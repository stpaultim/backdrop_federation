<?php
/**
 * @file
 * HTTP Signature signing and verification for ActivityPub.
 */

/**
 * Sign an outgoing HTTP request.
 *
 * @param string $url
 *   The target URL.
 * @param string $body
 *   The request body.
 * @param string $method
 *   The HTTP method (default: POST).
 *
 * @return array|false
 *   Array of headers to include, or FALSE on failure.
 */
function backdrop_federation_sign_request($url, $body, $method = 'POST') {
  $keypair = backdrop_federation_get_keypair();
  if (!$keypair) {
    watchdog('backdrop_federation', 'No keypair available for signing.', array(), WATCHDOG_ERROR);
    return FALSE;
  }

  $parsed_url = parse_url($url);
  $host = $parsed_url['host'];
  $path = isset($parsed_url['path']) ? $parsed_url['path'] : '/';
  if (isset($parsed_url['query'])) {
    $path .= '?' . $parsed_url['query'];
  }

  // Generate date in HTTP format.
  $date = gmdate('D, d M Y H:i:s T');

  // Calculate body digest.
  $digest = 'SHA-256=' . base64_encode(hash('sha256', $body, TRUE));

  // Build the signing string.
  $request_target = strtolower($method) . ' ' . $path;
  $signing_string = "(request-target): {$request_target}\n";
  $signing_string .= "host: {$host}\n";
  $signing_string .= "date: {$date}\n";
  $signing_string .= "digest: {$digest}";

  // Sign the string.
  $private_key = openssl_pkey_get_private($keypair['private_key']);
  if (!$private_key) {
    watchdog('backdrop_federation', 'Failed to load private key for signing.', array(), WATCHDOG_ERROR);
    return FALSE;
  }

  $signature = '';
  $success = openssl_sign($signing_string, $signature, $private_key, OPENSSL_ALGO_SHA256);

  if (!$success) {
    watchdog('backdrop_federation', 'Failed to sign request.', array(), WATCHDOG_ERROR);
    return FALSE;
  }

  $signature_b64 = base64_encode($signature);

  // Build the Signature header.
  $key_id = backdrop_federation_get_actor_uri() . '#main-key';
  $signature_header = sprintf(
    'keyId="%s",algorithm="rsa-sha256",headers="(request-target) host date digest",signature="%s"',
    $key_id,
    $signature_b64
  );

  return array(
    'Host' => $host,
    'Date' => $date,
    'Digest' => $digest,
    'Signature' => $signature_header,
    'Content-Type' => 'application/activity+json',
    'Accept' => 'application/activity+json, application/ld+json',
  );
}

/**
 * Verify an incoming HTTP signature.
 *
 * @return bool
 *   TRUE if the signature is valid, FALSE otherwise.
 */
function backdrop_federation_verify_signature() {
  // Get the Signature header.
  $signature_header = isset($_SERVER['HTTP_SIGNATURE']) ? $_SERVER['HTTP_SIGNATURE'] : '';

  if (empty($signature_header)) {
    watchdog('backdrop_federation', 'No Signature header in request.', array(), WATCHDOG_WARNING);
    return FALSE;
  }

  // Parse the Signature header.
  $params = _backdrop_federation_parse_signature_header($signature_header);

  if (!isset($params['keyId'], $params['signature'], $params['headers'])) {
    watchdog('backdrop_federation', 'Invalid Signature header format.', array(), WATCHDOG_WARNING);
    return FALSE;
  }

  // Replay protection: reject requests whose Date header is outside ±5 minutes.
  $date_header = isset($_SERVER['HTTP_DATE']) ? $_SERVER['HTTP_DATE'] : '';
  if (empty($date_header)) {
    watchdog('backdrop_federation', 'Missing Date header in signed request.', array(), WATCHDOG_WARNING);
    return FALSE;
  }
  $request_time = strtotime($date_header);
  if ($request_time === FALSE || abs(REQUEST_TIME - $request_time) > 300) {
    watchdog('backdrop_federation', 'Request Date header is too old, too far in the future, or invalid: @date', array(
      '@date' => $date_header,
    ), WATCHDOG_WARNING);
    return FALSE;
  }

  // Fetch the public key.
  $public_key = _backdrop_federation_fetch_public_key($params['keyId']);
  if (!$public_key) {
    watchdog('backdrop_federation', 'Could not fetch public key from @keyId', array('@keyId' => $params['keyId']), WATCHDOG_WARNING);
    return FALSE;
  }

  // Rebuild the signing string.
  $headers_list = explode(' ', $params['headers']);
  $signing_parts = array();

  foreach ($headers_list as $header_name) {
    if ($header_name === '(request-target)') {
      $method = strtolower($_SERVER['REQUEST_METHOD']);
      $path = $_SERVER['REQUEST_URI'];
      $signing_parts[] = "(request-target): {$method} {$path}";
    }
    else {
      $header_key = 'HTTP_' . strtoupper(str_replace('-', '_', $header_name));
      if (isset($_SERVER[$header_key])) {
        $signing_parts[] = $header_name . ': ' . $_SERVER[$header_key];
      }
      elseif ($header_name === 'host' && isset($_SERVER['HTTP_HOST'])) {
        $signing_parts[] = 'host: ' . $_SERVER['HTTP_HOST'];
      }
    }
  }

  $signing_string = implode("\n", $signing_parts);

  // Verify the signature.
  $signature = base64_decode($params['signature']);
  $key = openssl_pkey_get_public($public_key);

  if (!$key) {
    watchdog('backdrop_federation', 'Invalid public key format.', array(), WATCHDOG_WARNING);
    return FALSE;
  }

  $result = openssl_verify($signing_string, $signature, $key, OPENSSL_ALGO_SHA256);

  if ($result === 1) {
    return TRUE;
  }
  elseif ($result === 0) {
    // Temporary diagnostic logging — remove after root cause is identified.
    watchdog('backdrop_federation', 'Signature verification failed. keyId=@keyid | headers=@headers | HTTP_HOST=@host | REQUEST_URI=@uri | signing_string=@signing', array(
      '@keyid'   => $params['keyId'],
      '@headers' => $params['headers'],
      '@host'    => isset($_SERVER['HTTP_HOST']) ? $_SERVER['HTTP_HOST'] : '(missing)',
      '@uri'     => isset($_SERVER['REQUEST_URI']) ? $_SERVER['REQUEST_URI'] : '(missing)',
      '@signing' => $signing_string,
    ), WATCHDOG_WARNING);
    return FALSE;
  }
  else {
    watchdog('backdrop_federation', 'Error during signature verification: @error', array(
      '@error' => openssl_error_string(),
    ), WATCHDOG_WARNING);
    return FALSE;
  }
}

/**
 * Parse a Signature header into its components.
 *
 * @param string $header
 *   The Signature header value.
 *
 * @return array
 *   Associative array of signature parameters.
 */
function _backdrop_federation_parse_signature_header($header) {
  $params = array();

  // Match key="value" pairs.
  preg_match_all('/(\w+)="([^"]+)"/', $header, $matches, PREG_SET_ORDER);

  foreach ($matches as $match) {
    $params[$match[1]] = $match[2];
  }

  return $params;
}

/**
 * Fetch a public key from a key ID URL.
 *
 * @param string $key_id
 *   The key ID (usually actor URI with #main-key fragment).
 *
 * @return string|false
 *   The PEM-encoded public key, or FALSE on failure.
 */
function _backdrop_federation_fetch_public_key($key_id) {
  // Remove the fragment to get the actor URL.
  $actor_uri = preg_replace('/#.*$/', '', $key_id);

  $options = array(
    'headers' => array(
      'Accept' => 'application/activity+json, application/ld+json',
    ),
    'timeout' => 10,
  );

  $response = backdrop_http_request($actor_uri, $options);

  if ($response->code != 200) {
    watchdog('backdrop_federation', 'Failed to fetch actor for key verification: @code', array('@code' => $response->code), WATCHDOG_WARNING);
    return FALSE;
  }

  $data = json_decode($response->data, TRUE);

  if (!isset($data['publicKey']['publicKeyPem'])) {
    watchdog('backdrop_federation', 'Actor has no publicKey.', array(), WATCHDOG_WARNING);
    return FALSE;
  }

  // Verify the key ID matches.
  if (isset($data['publicKey']['id']) && $data['publicKey']['id'] !== $key_id) {
    watchdog('backdrop_federation', 'Key ID mismatch: expected @expected, got @got', array(
      '@expected' => $key_id,
      '@got' => $data['publicKey']['id'],
    ), WATCHDOG_WARNING);
    return FALSE;
  }

  return $data['publicKey']['publicKeyPem'];
}

/**
 * POST an activity to a remote inbox.
 *
 * @param string $inbox_uri
 *   The inbox URI to post to.
 * @param string $json
 *   The JSON activity body.
 *
 * @return bool
 *   TRUE on success, FALSE on failure.
 */
function backdrop_federation_post_to_inbox($inbox_uri, $json) {
  $headers = backdrop_federation_sign_request($inbox_uri, $json, 'POST');

  if (!$headers) {
    return FALSE;
  }

  $options = array(
    'method' => 'POST',
    'data' => $json,
    'headers' => $headers,
    'timeout' => 15,
  );

  $response = backdrop_http_request($inbox_uri, $options);

  if ($response->code >= 200 && $response->code < 300) {
    watchdog('backdrop_federation', 'Successfully posted to @inbox', array('@inbox' => $inbox_uri), WATCHDOG_DEBUG);
    return TRUE;
  }

  watchdog('backdrop_federation', 'Failed to post to @inbox: @code @error — Response: @body', array(
    '@inbox' => $inbox_uri,
    '@code' => $response->code,
    '@error' => isset($response->error) ? $response->error : '',
    '@body' => isset($response->data) ? substr($response->data, 0, 500) : '',
  ), WATCHDOG_WARNING);

  return FALSE;
}
