<?php
/**
 * @file
 * Page callbacks for Fediverse endpoints.
 */

/**
 * WebFinger endpoint callback.
 *
 * Handles /.well-known/webfinger requests for actor discovery.
 */
function backdrop_federation_webfinger_endpoint() {
  $resource = isset($_GET['resource']) ? $_GET['resource'] : '';

  if (empty($resource)) {
    backdrop_add_http_header('Status', '400 Bad Request');
    backdrop_add_http_header('Content-Type', 'application/json');
    echo json_encode(array('error' => 'Missing resource parameter'));
    backdrop_exit();
  }

  // Parse the resource (acct:name@domain).
  if (!preg_match('/^acct:([^@]+)@(.+)$/', $resource, $matches)) {
    backdrop_add_http_header('Status', '400 Bad Request');
    backdrop_add_http_header('Content-Type', 'application/json');
    echo json_encode(array('error' => 'Invalid resource format'));
    backdrop_exit();
  }

  $name = $matches[1];
  $domain = $matches[2];

  global $base_url;
  $host = parse_url($base_url, PHP_URL_HOST);
  $actor_name = backdrop_federation_get_actor_name();

  // Check if this is our actor.
  if ($name !== $actor_name || $domain !== $host) {
    backdrop_add_http_header('Status', '404 Not Found');
    backdrop_add_http_header('Content-Type', 'application/json');
    echo json_encode(array('error' => 'Resource not found'));
    backdrop_exit();
  }

  global $base_url;
  $result = array(
    'subject' => $resource,
    'aliases' => array(
      backdrop_federation_get_actor_uri(),
    ),
    'links' => array(
      array(
        'rel' => 'self',
        'type' => 'application/activity+json',
        'href' => backdrop_federation_get_actor_uri(),
      ),
      array(
        'rel' => 'http://ostatus.org/schema/1.0/subscribe',
        'template' => $base_url . '/fediverse/subscribe?uri={uri}',
      ),
    ),
  );

  backdrop_add_http_header('Content-Type', 'application/jrd+json');
  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  echo json_encode($result, JSON_UNESCAPED_SLASHES);
  backdrop_exit();
}

/**
 * NodeInfo well-known endpoint.
 *
 * Returns links to NodeInfo endpoints.
 */
function backdrop_federation_nodeinfo_wellknown() {
  global $base_url;

  $result = array(
    'links' => array(
      array(
        'rel' => 'http://nodeinfo.diaspora.software/ns/schema/2.1',
        'href' => $base_url . '/nodeinfo/2.1',
      ),
    ),
  );

  backdrop_add_http_header('Content-Type', 'application/json');
  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  echo json_encode($result, JSON_UNESCAPED_SLASHES);
  backdrop_exit();
}

/**
 * NodeInfo 2.1 endpoint callback.
 *
 * Returns server metadata.
 */
function backdrop_federation_nodeinfo_endpoint() {
  // Count users (active users who can create content).
  $user_count = db_select('users', 'u')
    ->condition('status', 1)
    ->condition('uid', 0, '>')
    ->countQuery()
    ->execute()
    ->fetchField();

  // Count published nodes.
  $post_count = db_select('node', 'n')
    ->condition('status', NODE_PUBLISHED)
    ->countQuery()
    ->execute()
    ->fetchField();

  $result = array(
    'version' => '2.1',
    'software' => array(
      'name' => 'backdrop',
      'version' => BACKDROP_VERSION,
      'repository' => 'https://github.com/backdrop/backdrop',
      'homepage' => 'https://backdropcms.org',
    ),
    'protocols' => array('activitypub'),
    'usage' => array(
      'users' => array(
        'total' => (int) $user_count,
      ),
      'localPosts' => (int) $post_count,
    ),
    'openRegistrations' => (bool) config_get('system.core', 'user_register'),
    'metadata' => new stdClass(),
  );

  backdrop_add_http_header('Content-Type', 'application/json; profile="http://nodeinfo.diaspora.software/ns/schema/2.1#"');
  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  echo json_encode($result, JSON_UNESCAPED_SLASHES);
  backdrop_exit();
}

/**
 * Actor endpoint callback.
 *
 * Returns the ActivityStreams Actor for this site.
 */
function backdrop_federation_actor_endpoint() {
  if (!config_get('backdrop_federation.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  $keypair = backdrop_federation_get_keypair();
  if (!$keypair) {
    watchdog('backdrop_federation', 'No keypair available for actor endpoint.', array(), WATCHDOG_ERROR);
    return MENU_NOT_FOUND;
  }

  global $base_url;
  $actor_uri = backdrop_federation_get_actor_uri();

  // Get the actor's published date from when the keypair was first created.
  $actor_created = db_select('backdrop_federation_keypair', 'k')
    ->fields('k', array('created'))
    ->orderBy('created', 'ASC')
    ->range(0, 1)
    ->execute()
    ->fetchField();

  $actor = array(
    '@context' => array(
      'https://www.w3.org/ns/activitystreams',
      'https://w3id.org/security/v1',
      array(
        'toot' => 'http://joinmastodon.org/ns#',
        'discoverable' => 'toot:discoverable',
      ),
    ),
    'id' => $actor_uri,
    'type' => 'Application',
    'preferredUsername' => backdrop_federation_get_actor_name(),
    'name' => backdrop_federation_get_actor_display_name(),
    'summary' => check_plain(backdrop_federation_get_actor_summary()),
    'url' => $base_url,
    'published' => $actor_created ? gmdate('c', $actor_created) : NULL,
    'inbox' => $base_url . '/fediverse/inbox',
    'outbox' => $base_url . '/fediverse/outbox',
    'followers' => $base_url . '/fediverse/followers',
    'manuallyApprovesFollowers' => FALSE,
    'discoverable' => TRUE,
    'publicKey' => array(
      'id' => $actor_uri . '#main-key',
      'owner' => $actor_uri,
      'publicKeyPem' => $keypair['public_key'],
    ),
  );

  // Add icon (avatar). Use the uploaded avatar file, falling back to the
  // site logo if none has been configured.
  $avatar_url = '';
  $fid = (int) config_get('backdrop_federation.settings', 'actor_avatar_fid');
  if ($fid) {
    $file = file_load($fid);
    if ($file) {
      $avatar_url = file_create_url($file->uri);
    }
  }
  if (empty($avatar_url)) {
    $logo = theme_get_setting('logo');
    if (!empty($logo)) {
      if (strpos($logo, 'http://') === 0 || strpos($logo, 'https://') === 0) {
        $avatar_url = $logo;
      }
      elseif (strpos($logo, '://') !== FALSE) {
        $avatar_url = file_create_url($logo);
      }
      else {
        $avatar_url = $base_url . '/' . ltrim($logo, '/');
      }
    }
  }
  if (!empty($avatar_url)) {
    $ext = strtolower(pathinfo(parse_url($avatar_url, PHP_URL_PATH), PATHINFO_EXTENSION));
    $media_types = array(
      'jpg'  => 'image/jpeg',
      'jpeg' => 'image/jpeg',
      'png'  => 'image/png',
      'gif'  => 'image/gif',
      'webp' => 'image/webp',
      'svg'  => 'image/svg+xml',
    );
    $actor['icon'] = array(
      'type' => 'Image',
      'mediaType' => isset($media_types[$ext]) ? $media_types[$ext] : 'image/jpeg',
      'url' => $avatar_url,
    );
  }

  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  return $actor;
}

/**
 * Inbox endpoint callback.
 *
 * Receives and processes incoming ActivityPub activities.
 */
function backdrop_federation_inbox_endpoint() {
  if (!config_get('backdrop_federation.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  // Only accept POST requests.
  if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    backdrop_add_http_header('Status', '405 Method Not Allowed');
    backdrop_add_http_header('Allow', 'POST');
    return array('error' => 'Method not allowed');
  }

  // Get and parse the request body.
  $body = file_get_contents('php://input');
  $activity = json_decode($body, TRUE);

  if (!$activity || !isset($activity['type'])) {
    backdrop_add_http_header('Status', '400 Bad Request');
    return array('error' => 'Invalid activity');
  }

  // Verify HTTP signature.
  module_load_include('inc', 'backdrop_federation', 'includes/backdrop_federation.http_signature');
  if (!backdrop_federation_verify_signature()) {
    backdrop_add_http_header('Status', '401 Unauthorized');
    return array('error' => 'Invalid signature');
  }

  // Blocklist check.
  $actor_uri = isset($activity['actor']) ? $activity['actor'] : '';
  if ($actor_uri && _backdrop_federation_is_blocked($actor_uri)) {
    backdrop_add_http_header('Status', '403 Forbidden');
    return array('error' => 'Forbidden');
  }

  // Rate limit check.
  if ($actor_uri && !_backdrop_federation_check_rate_limit($actor_uri)) {
    backdrop_add_http_header('Status', '429 Too Many Requests');
    backdrop_add_http_header('Retry-After', '60');
    return array('error' => 'Too Many Requests');
  }

  // Process the activity.
  switch ($activity['type']) {
    case 'Follow':
      return _backdrop_federation_handle_follow($activity);

    case 'Like':
      return _backdrop_federation_handle_like($activity);

    case 'Announce':
      module_invoke_all('backdrop_federation_inbox_activity', $activity);
      return _backdrop_federation_handle_announce($activity);

    case 'Undo':
      module_invoke_all('backdrop_federation_inbox_activity', $activity);
      $undo_type = isset($activity['object']['type']) ? $activity['object']['type'] : '';
      if ($undo_type === 'Follow') {
        return _backdrop_federation_handle_undo_follow($activity);
      }
      if ($undo_type === 'Like' || $undo_type === 'Announce') {
        return _backdrop_federation_handle_undo_reaction($activity, $undo_type);
      }
      break;

    case 'Create':
      module_invoke_all('backdrop_federation_inbox_activity', $activity);
      return _backdrop_federation_handle_create($activity);

    case 'Update':
      module_invoke_all('backdrop_federation_inbox_activity', $activity);
      return _backdrop_federation_handle_update($activity);

    case 'Delete':
      module_invoke_all('backdrop_federation_inbox_activity', $activity);
      return _backdrop_federation_handle_delete($activity);

    case 'Accept':
    case 'Reject':
      module_invoke_all('backdrop_federation_inbox_activity', $activity);
      return array('status' => 'ok');
  }

  // Unknown activity type — let sub-modules handle it if they want.
  module_invoke_all('backdrop_federation_inbox_activity', $activity);
  watchdog('backdrop_federation', 'Received unhandled activity type: @type', array('@type' => $activity['type']), WATCHDOG_DEBUG);
  return array('status' => 'ok');
}

/**
 * Check if an actor URI is on the blocklist.
 *
 * @param string $actor_uri
 *   The actor URI to check.
 *
 * @return bool
 *   TRUE if the actor or its domain is blocked.
 */
function _backdrop_federation_is_blocked($actor_uri) {
  // Check exact actor URI match.
  $blocked = db_select('backdrop_federation_blocklist', 'b')
    ->fields('b', array('id'))
    ->condition('value', $actor_uri)
    ->condition('type', 'actor')
    ->execute()
    ->fetchField();

  if ($blocked !== FALSE) {
    return TRUE;
  }

  // Check domain match.
  $domain = parse_url($actor_uri, PHP_URL_HOST);
  if ($domain) {
    $blocked = db_select('backdrop_federation_blocklist', 'b')
      ->fields('b', array('id'))
      ->condition('value', $domain)
      ->condition('type', 'domain')
      ->execute()
      ->fetchField();

    if ($blocked !== FALSE) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Check and update the rate limit for an actor.
 *
 * @param string $actor_uri
 *   The actor URI to check.
 *
 * @return bool
 *   TRUE if the request is within the rate limit, FALSE if exceeded.
 */
function _backdrop_federation_check_rate_limit($actor_uri) {
  $limit = (int) config_get('backdrop_federation.settings', 'rate_limit_requests');
  if (!$limit) {
    $limit = 20;
  }
  $window = (int) config_get('backdrop_federation.settings', 'rate_limit_window');
  if (!$window) {
    $window = 60;
  }

  $record = db_select('backdrop_federation_rate_limit', 'r')
    ->fields('r', array('id', 'request_count', 'window_start'))
    ->condition('actor_uri', $actor_uri)
    ->execute()
    ->fetchObject();

  $now = REQUEST_TIME;

  if (!$record || ($now - $record->window_start) >= $window) {
    // No record or window expired — start a fresh window.
    db_merge('backdrop_federation_rate_limit')
      ->key(array('actor_uri' => $actor_uri))
      ->fields(array(
        'actor_uri' => $actor_uri,
        'request_count' => 1,
        'window_start' => $now,
      ))
      ->execute();
    return TRUE;
  }

  if ($record->request_count >= $limit) {
    watchdog('backdrop_federation', 'Rate limit exceeded for actor @actor (@count requests in @window seconds)', array(
      '@actor' => $actor_uri,
      '@count' => $record->request_count,
      '@window' => $window,
    ), WATCHDOG_WARNING);
    return FALSE;
  }

  // Increment within current window.
  db_update('backdrop_federation_rate_limit')
    ->expression('request_count', 'request_count + 1')
    ->condition('actor_uri', $actor_uri)
    ->execute();

  return TRUE;
}

/**
 * Handle a Like activity.
 */
function _backdrop_federation_handle_like($activity) {
  return _backdrop_federation_handle_reaction($activity, 'Like');
}

/**
 * Handle an Announce (boost) activity.
 */
function _backdrop_federation_handle_announce($activity) {
  return _backdrop_federation_handle_reaction($activity, 'Announce');
}

/**
 * Shared handler for Like and Announce reactions.
 *
 * @param array $activity
 *   The incoming activity.
 * @param string $type
 *   'Like' or 'Announce'.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_reaction($activity, $type) {
  // Extract object URL — may be a string or array with 'id'.
  $object = isset($activity['object']) ? $activity['object'] : NULL;
  if (is_array($object)) {
    $object_url = isset($object['id']) ? $object['id'] : NULL;
  }
  else {
    $object_url = $object;
  }

  if (empty($object_url)) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  // Validate host matches local site.
  global $base_url;
  $local_host = parse_url($base_url, PHP_URL_HOST);
  $object_host = parse_url($object_url, PHP_URL_HOST);
  if ($object_host !== $local_host) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  // Extract nid from URL path.
  $object_path = parse_url($object_url, PHP_URL_PATH);
  if (!preg_match('/\/node\/(\d+)/', $object_path, $matches)) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }
  $nid = (int) $matches[1];

  // Load and validate the node.
  $node = node_load($nid);
  if (!$node || !$node->status || !_backdrop_federation_type_enabled($node->type)) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  $actor_uri = isset($activity['actor']) ? $activity['actor'] : '';

  // Duplicate check.
  $existing = db_select('backdrop_federation_reactions', 'r')
    ->fields('r', array('id'))
    ->condition('nid', $nid)
    ->condition('actor_uri', $actor_uri)
    ->condition('type', $type)
    ->execute()
    ->fetchField();

  if ($existing !== FALSE) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  // Insert reaction.
  db_insert('backdrop_federation_reactions')
    ->fields(array(
      'nid' => $nid,
      'actor_uri' => $actor_uri,
      'type' => $type,
      'created' => REQUEST_TIME,
    ))
    ->execute();

  watchdog('backdrop_federation', '@type from @actor on node @nid', array(
    '@type' => $type,
    '@actor' => $actor_uri,
    '@nid' => $nid,
  ), WATCHDOG_DEBUG);

  backdrop_add_http_header('Status', '202 Accepted');
  return array('status' => 'ok');
}

/**
 * Handle an Undo of a Like or Announce reaction.
 *
 * @param array $activity
 *   The Undo activity.
 * @param string $type
 *   'Like' or 'Announce'.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_undo_reaction($activity, $type) {
  // The liked/boosted object is nested: activity['object']['object'].
  $inner = isset($activity['object']['object']) ? $activity['object']['object'] : NULL;
  if (is_array($inner)) {
    $object_url = isset($inner['id']) ? $inner['id'] : NULL;
  }
  else {
    $object_url = $inner;
  }

  if (empty($object_url)) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  // Validate host.
  global $base_url;
  $local_host = parse_url($base_url, PHP_URL_HOST);
  $object_host = parse_url($object_url, PHP_URL_HOST);
  if ($object_host !== $local_host) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  // Extract nid.
  $object_path = parse_url($object_url, PHP_URL_PATH);
  if (!preg_match('/\/node\/(\d+)/', $object_path, $matches)) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }
  $nid = (int) $matches[1];

  $actor_uri = isset($activity['actor']) ? $activity['actor'] : '';

  db_delete('backdrop_federation_reactions')
    ->condition('nid', $nid)
    ->condition('actor_uri', $actor_uri)
    ->condition('type', $type)
    ->execute();

  watchdog('backdrop_federation', 'Undo @type from @actor on node @nid', array(
    '@type' => $type,
    '@actor' => $actor_uri,
    '@nid' => $nid,
  ), WATCHDOG_DEBUG);

  backdrop_add_http_header('Status', '202 Accepted');
  return array('status' => 'ok');
}

/**
 * Handle a Follow activity.
 *
 * @param array $activity
 *   The Follow activity.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_follow($activity) {
  $actor_uri = $activity['actor'];

  // Validate the actor.
  if (empty($actor_uri) || !filter_var($actor_uri, FILTER_VALIDATE_URL)) {
    backdrop_add_http_header('Status', '400 Bad Request');
    return array('error' => 'Invalid actor');
  }

  // Fetch the actor to get their inbox.
  $actor_data = _backdrop_federation_fetch_actor($actor_uri);
  if (!$actor_data) {
    backdrop_add_http_header('Status', '400 Bad Request');
    return array('error' => 'Could not fetch actor');
  }

  $inbox_uri = isset($actor_data['inbox']) ? $actor_data['inbox'] : NULL;
  $shared_inbox_uri = isset($actor_data['endpoints']['sharedInbox']) ? $actor_data['endpoints']['sharedInbox'] : NULL;

  if (!$inbox_uri) {
    backdrop_add_http_header('Status', '400 Bad Request');
    return array('error' => 'Actor has no inbox');
  }

  // Check if already following.
  $existing = db_select('backdrop_federation_followers', 'f')
    ->fields('f', array('id'))
    ->condition('actor_uri', $actor_uri)
    ->execute()
    ->fetchField();

  if (!$existing) {
    // Add to followers.
    db_insert('backdrop_federation_followers')
      ->fields(array(
        'actor_uri' => $actor_uri,
        'inbox_uri' => $inbox_uri,
        'shared_inbox_uri' => $shared_inbox_uri,
        'created' => REQUEST_TIME,
      ))
      ->execute();

    watchdog('backdrop_federation', 'New follower: @actor', array('@actor' => $actor_uri), WATCHDOG_INFO);
  }

  // Send Accept activity.
  _backdrop_federation_send_accept($activity, $inbox_uri);

  backdrop_add_http_header('Status', '202 Accepted');
  return array('status' => 'ok');
}

/**
 * Handle an Undo Follow activity.
 *
 * @param array $activity
 *   The Undo activity containing a Follow.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_undo_follow($activity) {
  $actor_uri = $activity['actor'];

  // Remove from followers.
  $deleted = db_delete('backdrop_federation_followers')
    ->condition('actor_uri', $actor_uri)
    ->execute();

  if ($deleted) {
    watchdog('backdrop_federation', 'Unfollowed by: @actor', array('@actor' => $actor_uri), WATCHDOG_INFO);
  }

  return array('status' => 'ok');
}

/**
 * Send an Accept activity in response to a Follow.
 *
 * @param array $follow_activity
 *   The original Follow activity.
 * @param string $inbox_uri
 *   The inbox to send the Accept to.
 */
function _backdrop_federation_send_accept($follow_activity, $inbox_uri) {
  global $base_url;

  $accept = array(
    '@context' => 'https://www.w3.org/ns/activitystreams',
    'id' => $base_url . '/fediverse/activity/' . backdrop_hash_base64(uniqid('accept', TRUE)),
    'type' => 'Accept',
    'actor' => backdrop_federation_get_actor_uri(),
    'object' => $follow_activity,
  );

  $json = json_encode($accept, JSON_UNESCAPED_SLASHES);

  module_load_include('inc', 'backdrop_federation', 'includes/backdrop_federation.http_signature');
  backdrop_federation_post_to_inbox($inbox_uri, $json);
}

/**
 * Fetch an actor from a remote server.
 *
 * @param string $actor_uri
 *   The actor URI to fetch.
 *
 * @return array|false
 *   The actor data or FALSE on failure.
 */
function _backdrop_federation_fetch_actor($actor_uri) {
  $options = array(
    'headers' => array(
      'Accept' => 'application/activity+json, application/ld+json',
    ),
    'timeout' => 10,
  );

  $response = backdrop_http_request($actor_uri, $options);

  if ($response->code != 200) {
    watchdog('backdrop_federation', 'Failed to fetch actor @uri: @code', array(
      '@uri' => $actor_uri,
      '@code' => $response->code,
    ), WATCHDOG_WARNING);
    return FALSE;
  }

  $data = json_decode($response->data, TRUE);
  if (!$data || !isset($data['inbox'])) {
    watchdog('backdrop_federation', 'Invalid actor data from @uri', array('@uri' => $actor_uri), WATCHDOG_WARNING);
    return FALSE;
  }

  return $data;
}

/**
 * Build a "replied via fediverse" attribution note for a comment body.
 *
 * @param string $actor_name
 *   The actor's preferredUsername.
 * @param string $actor_url
 *   The actor's profile URL.
 *
 * @return string
 *   HTML paragraph with a backdrop-federation-source class.
 */
function _backdrop_federation_build_source_note($actor_name, $actor_url) {
  $instance = $actor_url ? parse_url($actor_url, PHP_URL_HOST) : '';
  $handle = '@' . $actor_name . ($instance ? '@' . $instance : '');
  if ($actor_url) {
    $link = '<a href="' . check_url($actor_url) . '">' . check_plain($handle) . '</a>';
  }
  else {
    $link = check_plain($handle);
  }
  return '<p class="backdrop-federation-source">' . t('Replied via the fediverse — !link', array('!link' => $link)) . '</p>';
}

/**
 * Handle a Create activity — save a reply as a Backdrop comment.
 *
 * @param array $activity
 *   The Create activity.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_create($activity) {
  // Check the master switch.
  if (!config_get('backdrop_federation.settings', 'accept_replies')) {
    return array('status' => 'ok');
  }

  // Object must be an array with a recognised type.
  $object = isset($activity['object']) ? $activity['object'] : NULL;
  if (!is_array($object) || !in_array(isset($object['type']) ? $object['type'] : '', array('Note', 'Article'))) {
    return array('status' => 'ok');
  }

  // Only process replies.
  $in_reply_to = isset($object['inReplyTo']) ? $object['inReplyTo'] : NULL;
  if (empty($in_reply_to)) {
    return array('status' => 'ok');
  }

  // The inReplyTo value must belong to this site.
  global $base_url;
  $local_host = parse_url($base_url, PHP_URL_HOST);
  $reply_host = parse_url($in_reply_to, PHP_URL_HOST);
  if ($reply_host !== $local_host) {
    watchdog('backdrop_federation', 'Ignoring reply to non-local URL: @url', array('@url' => $in_reply_to), WATCHDOG_DEBUG);
    return array('status' => 'ok');
  }

  // Extract nid from the URL path.
  $reply_path = parse_url($in_reply_to, PHP_URL_PATH);
  if (!preg_match('/\/node\/(\d+)/', $reply_path, $matches)) {
    watchdog('backdrop_federation', 'Could not extract nid from inReplyTo URL: @url', array('@url' => $in_reply_to), WATCHDOG_DEBUG);
    return array('status' => 'ok');
  }
  $nid = (int) $matches[1];

  // Load and validate the node.
  $node = node_load($nid);
  if (!$node || !$node->status || !_backdrop_federation_type_enabled($node->type)) {
    return array('status' => 'ok');
  }

  // Prevent duplicate comments.
  $object_id = isset($object['id']) ? $object['id'] : NULL;
  if (empty($object_id)) {
    return array('status' => 'ok');
  }

  $existing_cid = db_select('backdrop_federation_received', 'r')
    ->fields('r', array('cid'))
    ->condition('object_id', substr($object_id, 0, 2048))
    ->execute()
    ->fetchField();

  if ($existing_cid !== FALSE) {
    backdrop_add_http_header('Status', '202 Accepted');
    return array('status' => 'ok');
  }

  // Fetch actor for display name / profile URL.
  $actor_uri = isset($activity['actor']) ? $activity['actor'] : '';
  $actor_data = $actor_uri ? _backdrop_federation_fetch_actor($actor_uri) : FALSE;
  $actor_name = '';
  $actor_url  = '';
  if ($actor_data) {
    $actor_name = isset($actor_data['preferredUsername']) ? $actor_data['preferredUsername'] : '';
    if (empty($actor_name) && isset($actor_data['name'])) {
      $actor_name = $actor_data['name'];
    }
    $actor_url = isset($actor_data['url']) ? $actor_data['url'] : $actor_uri;
  }
  if (empty($actor_name)) {
    $actor_name = $actor_uri;
  }

  // Sanitize content and append attribution note.
  $raw_content = isset($object['content']) ? $object['content'] : '';
  $content = filter_xss($raw_content);
  $subject  = truncate_utf8(strip_tags($content), 64, TRUE, TRUE);
  if (empty($subject)) {
    $subject = t('Fediverse reply');
  }
  $content .= "\n" . _backdrop_federation_build_source_note($actor_name, $actor_url);

  // Build the comment entity.
  $moderation = config_get('backdrop_federation.settings', 'comment_moderation');

  $comment = entity_create('comment', array(
    'nid'      => $nid,
    'pid'      => 0,
    'uid'      => 0,
    'name'     => $actor_name,
    'homepage' => $actor_url,
    'subject'  => $subject,
    'langcode' => LANGUAGE_NONE,
    'status'   => $moderation ? COMMENT_NOT_PUBLISHED : COMMENT_PUBLISHED,
  ));
  $comment->comment_body[LANGUAGE_NONE][0] = array(
    'value'  => $content,
    'format' => 'filtered_html',
  );

  comment_save($comment);

  // Record the mapping so updates/deletes can find this comment.
  db_insert('backdrop_federation_received')
    ->fields(array(
      'object_id' => substr($object_id, 0, 2048),
      'cid'       => $comment->cid,
      'nid'       => $nid,
      'created'   => REQUEST_TIME,
    ))
    ->execute();

  watchdog('backdrop_federation', 'Saved fediverse reply as comment @cid on node @nid.', array(
    '@cid' => $comment->cid,
    '@nid' => $nid,
  ), WATCHDOG_INFO);

  backdrop_add_http_header('Status', '202 Accepted');
  return array('status' => 'ok');
}

/**
 * Handle an Update activity — edit the matching Backdrop comment.
 *
 * @param array $activity
 *   The Update activity.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_update($activity) {
  $object = isset($activity['object']) ? $activity['object'] : NULL;
  if (!is_array($object) || empty($object['id'])) {
    return array('status' => 'ok');
  }

  $object_id = $object['id'];

  // Find the comment we stored for this object.
  $cid = db_select('backdrop_federation_received', 'r')
    ->fields('r', array('cid'))
    ->condition('object_id', substr($object_id, 0, 2048))
    ->execute()
    ->fetchField();

  if ($cid === FALSE) {
    // Not our comment — ignore silently.
    return array('status' => 'ok');
  }

  $comment = comment_load($cid);
  if (!$comment) {
    return array('status' => 'ok');
  }

  // Sanitize and rebuild, re-appending the attribution note.
  $raw_content = isset($object['content']) ? $object['content'] : '';
  $content = filter_xss($raw_content);
  $subject  = truncate_utf8(strip_tags($content), 64, TRUE, TRUE);
  if (empty($subject)) {
    $subject = t('Fediverse reply');
  }
  $content .= "\n" . _backdrop_federation_build_source_note($comment->name, $comment->homepage);

  $comment->subject = $subject;
  $comment->comment_body[LANGUAGE_NONE][0] = array(
    'value'  => $content,
    'format' => 'filtered_html',
  );

  comment_save($comment);

  watchdog('backdrop_federation', 'Updated fediverse comment @cid from Update activity.', array('@cid' => $cid), WATCHDOG_INFO);

  backdrop_add_http_header('Status', '202 Accepted');
  return array('status' => 'ok');
}

/**
 * Handle a Delete activity — remove the matching Backdrop comment.
 *
 * @param array $activity
 *   The Delete activity.
 *
 * @return array
 *   Response array.
 */
function _backdrop_federation_handle_delete($activity) {
  $object = isset($activity['object']) ? $activity['object'] : NULL;

  // The object may be a plain string URI or an array with an 'id' key.
  if (is_string($object)) {
    $object_id = $object;
  }
  elseif (is_array($object) && !empty($object['id'])) {
    $object_id = $object['id'];
  }
  else {
    return array('status' => 'ok');
  }

  // Find the comment.
  $row = db_select('backdrop_federation_received', 'r')
    ->fields('r', array('cid'))
    ->condition('object_id', substr($object_id, 0, 2048))
    ->execute()
    ->fetchObject();

  if (!$row) {
    return array('status' => 'ok');
  }

  $cid = $row->cid;
  comment_delete($cid);

  db_delete('backdrop_federation_received')
    ->condition('object_id', substr($object_id, 0, 2048))
    ->execute();

  watchdog('backdrop_federation', 'Deleted fediverse comment @cid from Delete activity.', array('@cid' => $cid), WATCHDOG_INFO);

  backdrop_add_http_header('Status', '202 Accepted');
  return array('status' => 'ok');
}

/**
 * Subscribe form - allows fediverse users to follow this site.
 */
function backdrop_federation_subscribe_form($form, &$form_state) {
  global $base_url;
  $actor_name = backdrop_federation_get_actor_name();
  $host = parse_url($base_url, PHP_URL_HOST);
  $handle = '@' . $actor_name . '@' . $host;

  $form['intro'] = array(
    '#markup' => '<p>' . t('Enter your Fediverse address to follow %handle.', array('%handle' => $handle)) . '</p>',
  );

  // Pre-fill from ?uri= query param (OStatus remote follow flow).
  // The uri may be in acct:user@domain or @user@domain format.
  $default_address = '';
  if (!empty($_GET['uri'])) {
    $uri = check_plain($_GET['uri']);
    // Strip acct: prefix if present.
    if (strpos($uri, 'acct:') === 0) {
      $uri = substr($uri, 5);
    }
    $default_address = $uri;
  }

  $form['backdrop_federation_address'] = array(
    '#type' => 'textfield',
    '#title' => t('Your Fediverse address'),
    '#description' => t('For example: @user@mastodon.social'),
    '#required' => TRUE,
    '#attributes' => array('placeholder' => '@user@mastodon.social'),
    '#default_value' => $default_address,
    '#size' => 40,
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Follow'),
  );

  return $form;
}

/**
 * Validation for the subscribe form.
 */
function backdrop_federation_subscribe_form_validate($form, &$form_state) {
  $address = trim($form_state['values']['backdrop_federation_address']);
  $address = ltrim($address, '@');

  if (!preg_match('/^[^@\s]+@[^@\s]+\.[^@\s]+$/', $address)) {
    form_set_error('backdrop_federation_address', t('Please enter a valid Fediverse address (e.g., user@mastodon.social).'));
  }
}

/**
 * Submit handler for the subscribe form.
 *
 * Redirects to the user's fediverse instance to complete the follow.
 */
function backdrop_federation_subscribe_form_submit($form, &$form_state) {
  $address = ltrim(trim($form_state['values']['backdrop_federation_address']), '@');
  $parts = explode('@', $address, 2);
  $domain = $parts[1];

  $actor_uri = backdrop_federation_get_actor_uri();
  $redirect_url = 'https://' . $domain . '/authorize_follow?uri=' . urlencode($actor_uri);

  backdrop_goto($redirect_url);
}

/**
 * Outbox endpoint callback.
 *
 * Returns an OrderedCollection of activities.
 */
function backdrop_federation_outbox_endpoint() {
  if (!config_get('backdrop_federation.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  global $base_url;

  // Get total count.
  $total = db_select('backdrop_federation_outbox', 'o')
    ->countQuery()
    ->execute()
    ->fetchField();

  $page = isset($_GET['page']) ? (int) $_GET['page'] : 0;
  $per_page = 20;

  if ($page > 0 || isset($_GET['page'])) {
    // Return a page of items.
    $activities = db_select('backdrop_federation_outbox', 'o')
      ->fields('o', array('activity_json'))
      ->orderBy('created', 'DESC')
      ->range($page * $per_page, $per_page)
      ->execute()
      ->fetchCol();

    $items = array_map('json_decode', $activities);

    return array(
      '@context' => 'https://www.w3.org/ns/activitystreams',
      'id' => $base_url . '/fediverse/outbox?page=' . $page,
      'type' => 'OrderedCollectionPage',
      'partOf' => $base_url . '/fediverse/outbox',
      'orderedItems' => $items,
    );
  }

  // Return collection metadata.
  $result = array(
    '@context' => 'https://www.w3.org/ns/activitystreams',
    'id' => $base_url . '/fediverse/outbox',
    'type' => 'OrderedCollection',
    'totalItems' => (int) $total,
  );

  if ($total > 0) {
    $result['first'] = $base_url . '/fediverse/outbox?page=0';
  }

  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  return $result;
}

/**
 * Followers endpoint callback.
 *
 * Returns an OrderedCollection of followers.
 */
function backdrop_federation_followers_endpoint() {
  if (!config_get('backdrop_federation.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  global $base_url;

  // Get total count.
  $total = db_select('backdrop_federation_followers', 'f')
    ->countQuery()
    ->execute()
    ->fetchField();

  // Serve a paginated view if requested.
  if (isset($_GET['page'])) {
    $result = array(
      '@context' => 'https://www.w3.org/ns/activitystreams',
      'id' => $base_url . '/fediverse/followers?page=1',
      'type' => 'OrderedCollectionPage',
      'partOf' => $base_url . '/fediverse/followers',
      'totalItems' => (int) $total,
      'orderedItems' => array(),
    );
    backdrop_add_http_header('Access-Control-Allow-Origin', '*');
    return $result;
  }

  // Return the collection summary with a first-page link so remote servers
  // trust totalItems. Follower URIs are not exposed to preserve privacy.
  $result = array(
    '@context' => 'https://www.w3.org/ns/activitystreams',
    'id' => $base_url . '/fediverse/followers',
    'type' => 'OrderedCollection',
    'totalItems' => (int) $total,
    'first' => $base_url . '/fediverse/followers?page=1',
  );

  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  return $result;
}
