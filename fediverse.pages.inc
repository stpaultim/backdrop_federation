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
function fediverse_webfinger_endpoint() {
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
  $actor_name = fediverse_get_actor_name();

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
      fediverse_get_actor_uri(),
    ),
    'links' => array(
      array(
        'rel' => 'self',
        'type' => 'application/activity+json',
        'href' => fediverse_get_actor_uri(),
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
function fediverse_nodeinfo_wellknown() {
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
function fediverse_nodeinfo_endpoint() {
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
function fediverse_actor_endpoint() {
  if (!config_get('fediverse.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  $keypair = fediverse_get_keypair();
  if (!$keypair) {
    watchdog('fediverse', 'No keypair available for actor endpoint.', array(), WATCHDOG_ERROR);
    return MENU_NOT_FOUND;
  }

  global $base_url;
  $actor_uri = fediverse_get_actor_uri();

  // Get the actor's published date from when the keypair was first created.
  $actor_created = db_select('fediverse_keypair', 'k')
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
    'preferredUsername' => fediverse_get_actor_name(),
    'name' => fediverse_get_actor_display_name(),
    'summary' => check_plain(fediverse_get_actor_summary()),
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

  // Add icon if site has a logo.
  $logo = theme_get_setting('logo');
  if (!empty($logo)) {
    $actor['icon'] = array(
      'type' => 'Image',
      'mediaType' => 'image/png',
      'url' => file_create_url($logo),
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
function fediverse_inbox_endpoint() {
  if (!config_get('fediverse.settings', 'enabled')) {
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
  module_load_include('inc', 'fediverse', 'includes/fediverse.http_signature');
  if (!fediverse_verify_signature()) {
    backdrop_add_http_header('Status', '401 Unauthorized');
    return array('error' => 'Invalid signature');
  }

  // Process the activity.
  switch ($activity['type']) {
    case 'Follow':
      return _fediverse_handle_follow($activity);

    case 'Undo':
      if (isset($activity['object']['type']) && $activity['object']['type'] === 'Follow') {
        return _fediverse_handle_undo_follow($activity);
      }
      break;

    case 'Accept':
    case 'Reject':
      // Acknowledgements - just log and accept.
      watchdog('fediverse', 'Received @type activity from @actor', array(
        '@type' => $activity['type'],
        '@actor' => $activity['actor'],
      ), WATCHDOG_DEBUG);
      return array('status' => 'ok');
  }

  // Unknown activity type - accept but don't process.
  watchdog('fediverse', 'Received unhandled activity type: @type', array('@type' => $activity['type']), WATCHDOG_DEBUG);
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
function _fediverse_handle_follow($activity) {
  $actor_uri = $activity['actor'];

  // Validate the actor.
  if (empty($actor_uri) || !filter_var($actor_uri, FILTER_VALIDATE_URL)) {
    backdrop_add_http_header('Status', '400 Bad Request');
    return array('error' => 'Invalid actor');
  }

  // Fetch the actor to get their inbox.
  $actor_data = _fediverse_fetch_actor($actor_uri);
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
  $existing = db_select('fediverse_followers', 'f')
    ->fields('f', array('id'))
    ->condition('actor_uri', $actor_uri)
    ->execute()
    ->fetchField();

  if (!$existing) {
    // Add to followers.
    db_insert('fediverse_followers')
      ->fields(array(
        'actor_uri' => $actor_uri,
        'inbox_uri' => $inbox_uri,
        'shared_inbox_uri' => $shared_inbox_uri,
        'created' => REQUEST_TIME,
      ))
      ->execute();

    watchdog('fediverse', 'New follower: @actor', array('@actor' => $actor_uri), WATCHDOG_INFO);
  }

  // Send Accept activity.
  _fediverse_send_accept($activity, $inbox_uri);

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
function _fediverse_handle_undo_follow($activity) {
  $actor_uri = $activity['actor'];

  // Remove from followers.
  $deleted = db_delete('fediverse_followers')
    ->condition('actor_uri', $actor_uri)
    ->execute();

  if ($deleted) {
    watchdog('fediverse', 'Unfollowed by: @actor', array('@actor' => $actor_uri), WATCHDOG_INFO);
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
function _fediverse_send_accept($follow_activity, $inbox_uri) {
  global $base_url;

  $accept = array(
    '@context' => 'https://www.w3.org/ns/activitystreams',
    'id' => $base_url . '/fediverse/activity/' . backdrop_hash_base64(uniqid('accept', TRUE)),
    'type' => 'Accept',
    'actor' => fediverse_get_actor_uri(),
    'object' => $follow_activity,
  );

  $json = json_encode($accept, JSON_UNESCAPED_SLASHES);

  module_load_include('inc', 'fediverse', 'includes/fediverse.http_signature');
  fediverse_post_to_inbox($inbox_uri, $json);
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
function _fediverse_fetch_actor($actor_uri) {
  $options = array(
    'headers' => array(
      'Accept' => 'application/activity+json, application/ld+json',
    ),
  );

  $response = backdrop_http_request($actor_uri, $options);

  if ($response->code != 200) {
    watchdog('fediverse', 'Failed to fetch actor @uri: @code', array(
      '@uri' => $actor_uri,
      '@code' => $response->code,
    ), WATCHDOG_WARNING);
    return FALSE;
  }

  $data = json_decode($response->data, TRUE);
  if (!$data || !isset($data['inbox'])) {
    watchdog('fediverse', 'Invalid actor data from @uri', array('@uri' => $actor_uri), WATCHDOG_WARNING);
    return FALSE;
  }

  return $data;
}

/**
 * Subscribe form - allows fediverse users to follow this site.
 */
function fediverse_subscribe_form($form, &$form_state) {
  global $base_url;
  $actor_name = fediverse_get_actor_name();
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

  $form['fediverse_address'] = array(
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
function fediverse_subscribe_form_validate($form, &$form_state) {
  $address = trim($form_state['values']['fediverse_address']);
  $address = ltrim($address, '@');

  if (!preg_match('/^[^@\s]+@[^@\s]+\.[^@\s]+$/', $address)) {
    form_set_error('fediverse_address', t('Please enter a valid Fediverse address (e.g., user@mastodon.social).'));
  }
}

/**
 * Submit handler for the subscribe form.
 *
 * Redirects to the user's fediverse instance to complete the follow.
 */
function fediverse_subscribe_form_submit($form, &$form_state) {
  $address = ltrim(trim($form_state['values']['fediverse_address']), '@');
  $parts = explode('@', $address, 2);
  $domain = $parts[1];

  $actor_uri = fediverse_get_actor_uri();
  $redirect_url = 'https://' . $domain . '/authorize_follow?uri=' . urlencode($actor_uri);

  backdrop_goto($redirect_url);
}

/**
 * Outbox endpoint callback.
 *
 * Returns an OrderedCollection of activities.
 */
function fediverse_outbox_endpoint() {
  if (!config_get('fediverse.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  global $base_url;

  // Get total count.
  $total = db_select('fediverse_outbox', 'o')
    ->countQuery()
    ->execute()
    ->fetchField();

  $page = isset($_GET['page']) ? (int) $_GET['page'] : 0;
  $per_page = 20;

  if ($page > 0 || isset($_GET['page'])) {
    // Return a page of items.
    $activities = db_select('fediverse_outbox', 'o')
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
function fediverse_followers_endpoint() {
  if (!config_get('fediverse.settings', 'enabled')) {
    return MENU_NOT_FOUND;
  }

  global $base_url;

  // Get total count.
  $total = db_select('fediverse_followers', 'f')
    ->countQuery()
    ->execute()
    ->fetchField();

  // For privacy, we only return the count, not the actual followers.
  $result = array(
    '@context' => 'https://www.w3.org/ns/activitystreams',
    'id' => $base_url . '/fediverse/followers',
    'type' => 'OrderedCollection',
    'totalItems' => (int) $total,
  );

  backdrop_add_http_header('Access-Control-Allow-Origin', '*');
  return $result;
}
