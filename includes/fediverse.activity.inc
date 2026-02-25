<?php
/**
 * @file
 * Activity building helpers for ActivityPub.
 */

/**
 * Build an ActivityPub activity from a node.
 *
 * @param Node $node
 *   The node to build an activity for.
 * @param string $type
 *   The activity type: Create, Update, or Delete.
 *
 * @return array
 *   The activity array.
 */
function fediverse_build_activity(Node $node, $type) {
  global $base_url;

  $actor_uri = fediverse_get_actor_uri();
  $node_url = url('node/' . $node->nid, array('absolute' => TRUE));

  // Generate a unique activity ID.
  $activity_id = $base_url . '/fediverse/activity/' . $node->nid . '/' . strtolower($type) . '/' . REQUEST_TIME;

  // Build the object (Note or Article).
  $object = _fediverse_build_object($node);

  // For Delete activities, we just need a Tombstone.
  if ($type === 'Delete') {
    $object = array(
      'id' => $node_url,
      'type' => 'Tombstone',
    );
  }

  $activity = array(
    '@context' => 'https://www.w3.org/ns/activitystreams',
    'id' => $activity_id,
    'type' => $type,
    'actor' => $actor_uri,
    'published' => gmdate('c', $node->created),
    'to' => array('https://www.w3.org/ns/activitystreams#Public'),
    'cc' => array($base_url . '/fediverse/followers'),
    'object' => $object,
  );

  return $activity;
}

/**
 * Build an ActivityPub object from a node.
 *
 * @param Node $node
 *   The node to build an object for.
 *
 * @return array
 *   The object array.
 */
function _fediverse_build_object(Node $node) {
  global $base_url;

  $actor_uri = fediverse_get_actor_uri();
  $node_url = url('node/' . $node->nid, array('absolute' => TRUE));

  // Determine object type based on node type.
  // Articles and pages become Article, others become Note.
  $object_type = in_array($node->type, array('article', 'page')) ? 'Article' : 'Note';

  // Get the configured text field for this content type, falling back to body.
  $field_mapping = config_get('fediverse.settings', 'field_mapping');
  $text_field = !empty($field_mapping[$node->type]) ? $field_mapping[$node->type] : 'body';

  // Get the body content.
  $body = '';
  if (!empty($node->{$text_field}[LANGUAGE_NONE][0]['value'])) {
    $body = $node->{$text_field}[LANGUAGE_NONE][0]['value'];
    $format = isset($node->{$text_field}[LANGUAGE_NONE][0]['format']) ? $node->{$text_field}[LANGUAGE_NONE][0]['format'] : NULL;
    $body = check_markup($body, $format);
  }

  // Build summary from teaser or truncate body.
  $summary = '';
  if (!empty($node->{$text_field}[LANGUAGE_NONE][0]['summary'])) {
    $summary = check_plain($node->{$text_field}[LANGUAGE_NONE][0]['summary']);
  }
  elseif (strlen(strip_tags($body)) > 500) {
    $summary = truncate_utf8(strip_tags($body), 500, TRUE, TRUE);
  }

  $object = array(
    'id' => $node_url,
    'type' => $object_type,
    'attributedTo' => $actor_uri,
    'url' => $node_url,
    'published' => gmdate('c', $node->created),
    'updated' => gmdate('c', $node->changed),
    'to' => array('https://www.w3.org/ns/activitystreams#Public'),
    'cc' => array($base_url . '/fediverse/followers'),
    'content' => $body,
  );

  // Add name for Article type.
  if ($object_type === 'Article') {
    $object['name'] = check_plain($node->title);
  }

  // Add summary if available.
  if (!empty($summary)) {
    $object['summary'] = $summary;
  }

  // Add images if present.
  $images = _fediverse_get_node_images($node);
  if (!empty($images)) {
    $object['attachment'] = $images;
  }

  return $object;
}

/**
 * Get images from a node for attachment.
 *
 * @param Node $node
 *   The node to get images from.
 *
 * @return array
 *   Array of image attachment objects.
 */
function _fediverse_get_node_images(Node $node) {
  $images = array();

  // Find all image fields on this content type dynamically.
  $instances = field_info_instances('node', $node->type);
  foreach ($instances as $field_name => $instance) {
    $field = field_info_field($field_name);
    if ($field['type'] !== 'image') {
      continue;
    }
    if (empty($node->{$field_name}[LANGUAGE_NONE])) {
      continue;
    }
    foreach ($node->{$field_name}[LANGUAGE_NONE] as $item) {
      if (empty($item['fid'])) {
        continue;
      }
      // uri/filemime may not be set on the node object at hook_node_insert time;
      // fall back to loading the managed file record directly.
      if (empty($item['uri'])) {
        $file = file_load($item['fid']);
        if (!$file) {
          continue;
        }
        $item['uri'] = $file->uri;
        $item['filemime'] = $file->filemime;
      }
      $image = array(
        'type' => 'Image',
        'mediaType' => isset($item['filemime']) ? $item['filemime'] : 'image/jpeg',
        'url' => file_create_url($item['uri']),
      );
      if (!empty($item['alt'])) {
        $image['name'] = $item['alt'];
      }
      $images[] = $image;
    }
  }

  return $images;
}

/**
 * Queue an activity for delivery to followers.
 *
 * @param Node $node
 *   The node that triggered the activity.
 * @param string $type
 *   The activity type: Create, Update, or Delete.
 */
function _fediverse_queue_activity(Node $node, $type) {
  $activity = fediverse_build_activity($node, $type);
  $json = json_encode($activity, JSON_UNESCAPED_SLASHES);

  // Determine object type.
  $object_type = 'Note';
  if (isset($activity['object']['type'])) {
    $object_type = $activity['object']['type'];
  }

  // Store in outbox.
  $activity_db_id = db_insert('fediverse_outbox')
    ->fields(array(
      'activity_id' => $activity['id'],
      'activity_type' => $type,
      'object_type' => $object_type,
      'nid' => $node->nid,
      'activity_json' => $json,
      'created' => REQUEST_TIME,
    ))
    ->execute();

  // Get all followers and queue delivery.
  $followers = db_select('fediverse_followers', 'f')
    ->fields('f', array('inbox_uri', 'shared_inbox_uri'))
    ->execute();

  $queue = BackdropQueue::get('fediverse_activity_delivery');
  $queued_inboxes = array();

  foreach ($followers as $follower) {
    // Prefer shared inbox for efficiency.
    $inbox = !empty($follower->shared_inbox_uri) ? $follower->shared_inbox_uri : $follower->inbox_uri;

    // Avoid queueing the same shared inbox multiple times.
    if (in_array($inbox, $queued_inboxes)) {
      continue;
    }
    $queued_inboxes[] = $inbox;

    $queue->createItem(array(
      'activity_id' => $activity_db_id,
      'inbox_uri' => $inbox,
    ));
  }

  watchdog('fediverse', 'Queued @type activity for node @nid to @count inboxes.', array(
    '@type' => $type,
    '@nid' => $node->nid,
    '@count' => count($queued_inboxes),
  ), WATCHDOG_INFO);
}
