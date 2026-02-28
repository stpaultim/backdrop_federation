<?php
/**
 * @file
 * Admin configuration form for Fediverse module.
 */

/**
 * Admin settings form.
 */
function backdrop_federation_admin_settings_form($form, &$form_state) {
  $config = config('backdrop_federation.settings');

  $form['#config'] = 'backdrop_federation.settings';

  $form['enabled'] = array(
    '#type' => 'checkbox',
    '#title' => t('Enable Fediverse federation'),
    '#description' => t('When enabled, this site will respond to ActivityPub requests and federate content.'),
    '#default_value' => $config->get('enabled'),
  );

  $form['actor'] = array(
    '#type' => 'fieldset',
    '#title' => t('Actor settings'),
    '#collapsible' => TRUE,
  );

  global $base_url;
  $host = parse_url($base_url, PHP_URL_HOST);
  $actor_name = $config->get('actor_name') ?: 'site';

  $form['actor']['actor_name'] = array(
    '#type' => 'textfield',
    '#title' => t('Actor username'),
    '#description' => t('The username for this site\'s actor. Users will find this site as <strong>@%name@%host</strong>.', array(
      '%name' => $actor_name,
      '%host' => $host,
    )),
    '#default_value' => $config->get('actor_name'),
    '#field_prefix' => '@',
    '#size' => 30,
    '#maxlength' => 64,
    '#element_validate' => array('_backdrop_federation_validate_actor_name'),
  );

  $form['actor']['actor_display_name'] = array(
    '#type' => 'textfield',
    '#title' => t('Display name'),
    '#description' => t('The display name shown in Fediverse clients. Defaults to site name if empty.'),
    '#default_value' => $config->get('actor_display_name'),
    '#placeholder' => config_get('system.core', 'site_name'),
  );

  $form['actor']['actor_summary'] = array(
    '#type' => 'textarea',
    '#title' => t('Bio'),
    '#description' => t('A brief description of this site. Defaults to site slogan if empty.'),
    '#default_value' => $config->get('actor_summary'),
    '#placeholder' => config_get('system.core', 'site_slogan'),
    '#rows' => 3,
  );

  $form['actor']['actor_avatar_fid'] = array(
    '#type' => 'managed_file',
    '#title' => t('Avatar'),
    '#description' => t('Profile picture shown in Fediverse clients (e.g. Mastodon). Square images work best; 400×400 px or larger is recommended. Falls back to the site logo if left empty.'),
    '#default_value' => $config->get('actor_avatar_fid') ?: 0,
    '#upload_location' => 'public://backdrop_federation/',
    '#upload_validators' => array(
      'file_validate_extensions' => array('png jpg jpeg gif webp'),
      'file_validate_size' => array(2 * 1024 * 1024),
    ),
    '#browser_view' => 'image_library',
  );

  // Content types.
  $form['content'] = array(
    '#type' => 'fieldset',
    '#title' => t('Content federation'),
    '#collapsible' => TRUE,
  );

  $node_types = node_type_get_names();
  $enabled_types = $config->get('content_types') ?: array();
  $field_mapping = $config->get('field_mapping') ?: array();
  $object_type_mapping = $config->get('object_type_mapping') ?: array();

  $form['content']['type_label'] = array(
    '#markup' => '<label>' . t('Content types to federate') . '</label>'
      . '<div class="description">' . t('Select which content types should be published to followers when created or updated. Choose which field provides the post body for each enabled type.') . '</div>',
  );

  // Per-type container — each content type gets its own checkbox and
  // (conditionally visible) field mapping select directly beneath it.
  $form['content']['per_type'] = array(
    '#type' => 'container',
    '#tree' => TRUE,
  );

  foreach ($node_types as $type => $name) {
    $form['content']['per_type'][$type]['enabled'] = array(
      '#type' => 'checkbox',
      '#title' => check_plain($name),
      '#default_value' => in_array($type, $enabled_types) ? 1 : 0,
    );

    $states_condition = array(
      'visible' => array(
        ':input[name="per_type[' . $type . '][enabled]"]' => array('checked' => TRUE),
      ),
    );

    $form['content']['per_type'][$type]['object_type'] = array(
      '#type' => 'select',
      '#title' => t('Federate as'),
      '#description' => t('<strong>Post (Note)</strong> — standard short-form Fediverse post, content only. <strong>Article</strong> — includes the node title; better for long-form content.'),
      '#options' => array(
        'Note'    => t('Post (Note)'),
        'Article' => t('Article'),
      ),
      '#default_value' => isset($object_type_mapping[$type]) ? $object_type_mapping[$type] : 'Note',
      '#prefix' => '<div style="margin-left:1.5em;">',
      '#suffix' => '</div>',
      '#states' => $states_condition,
    );

    $text_fields = _backdrop_federation_get_text_fields($type);
    if (!empty($text_fields)) {
      $default = isset($field_mapping[$type]) ? $field_mapping[$type] : (isset($text_fields['body']) ? 'body' : key($text_fields));
      $form['content']['per_type'][$type]['field'] = array(
        '#type' => 'select',
        '#title' => t('Body field'),
        '#description' => t('Field used as the post body when federating this content type.'),
        '#options' => $text_fields,
        '#default_value' => $default,
        '#prefix' => '<div style="margin-left:1.5em;">',
        '#suffix' => '</div>',
        '#states' => $states_condition,
      );
    }
  }

  $form['content']['accept_replies'] = array(
    '#type' => 'checkbox',
    '#title' => t('Accept fediverse replies as comments'),
    '#description' => t('When enabled, replies to federated posts on the fediverse will be saved as comments on the corresponding node.'),
    '#default_value' => $config->get('accept_replies'),
  );

  $form['content']['comment_moderation'] = array(
    '#type' => 'checkbox',
    '#title' => t('Hold fediverse replies for moderation before publishing'),
    '#description' => t('Uncheck to publish replies immediately as comments.'),
    '#default_value' => $config->get('comment_moderation'),
    '#states' => array(
      'visible' => array(
        ':input[name="accept_replies"]' => array('checked' => TRUE),
      ),
    ),
  );

  // Status information.
  $form['status'] = array(
    '#type' => 'fieldset',
    '#title' => t('Status'),
    '#collapsible' => TRUE,
  );

  // Follower count.
  $follower_count = db_select('backdrop_federation_followers', 'f')
    ->countQuery()
    ->execute()
    ->fetchField();

  $follower_link = l(
    format_plural($follower_count, '1 follower', '@count followers'),
    'admin/config/services/fediverse/followers'
  );
  $form['status']['follower_count'] = array(
    '#type' => 'item',
    '#title' => t('Followers'),
    '#markup' => $follower_link,
  );

  // Keypair status.
  $keypair = backdrop_federation_get_keypair();
  $form['status']['keypair_status'] = array(
    '#type' => 'item',
    '#title' => t('Keypair'),
    '#markup' => $keypair ? t('Generated and ready') : t('<span class="error">Missing - try reinstalling module</span>'),
  );

  // Blocklist link.
  $blocklist_count = db_select('backdrop_federation_blocklist', 'b')
    ->countQuery()
    ->execute()
    ->fetchField();

  $form['status']['blocklist'] = array(
    '#type' => 'item',
    '#title' => t('Blocklist'),
    '#markup' => l(
      format_plural($blocklist_count, '1 entry', '@count entries'),
      'admin/config/services/fediverse/blocklist'
    ),
  );

  // Actor URL.
  $form['status']['actor_url'] = array(
    '#type' => 'item',
    '#title' => t('Actor URL'),
    '#markup' => l(backdrop_federation_get_actor_uri(), 'fediverse/actor'),
  );

  // WebFinger URL.
  $webfinger_url = $base_url . '/.well-known/webfinger?resource=acct:' . $actor_name . '@' . $host;
  $form['status']['webfinger_url'] = array(
    '#type' => 'item',
    '#title' => t('WebFinger URL'),
    '#markup' => l($webfinger_url, '.well-known/webfinger', array(
      'query' => array('resource' => 'acct:' . $actor_name . '@' . $host),
    )),
  );

  // Maintenance.
  $form['maintenance'] = array(
    '#type' => 'fieldset',
    '#title' => t('Maintenance'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );

  $outbox_count = db_select('backdrop_federation_outbox', 'o')
    ->countQuery()
    ->execute()
    ->fetchField();

  $form['maintenance']['redeliver_description'] = array(
    '#markup' => '<p>' . t('Re-queue all posts in the outbox for delivery to current followers. Useful if posts were published before any followers existed. Run cron after clicking to process the queue.') . '</p>',
  );

  $form['maintenance']['redeliver'] = array(
    '#type' => 'submit',
    '#value' => t('Re-deliver all posts (@count)', array('@count' => $outbox_count)),
    '#submit' => array('backdrop_federation_admin_redeliver_submit'),
    '#disabled' => ($outbox_count == 0 || $follower_count == 0),
  );

  $form['actions']['#type'] = 'actions';
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save configuration'),
  );

  return $form;
}

/**
 * Get available text fields for a node type, suitable for use as body content.
 *
 * @param string $node_type
 *   The node type machine name.
 *
 * @return array
 *   Array of field_name => label for text fields on this content type.
 */
function _backdrop_federation_get_text_fields($node_type) {
  $text_types = array('text_with_summary', 'text_long', 'text');
  $options = array();
  foreach (field_info_instances('node', $node_type) as $field_name => $instance) {
    $field = field_info_field($field_name);
    if (in_array($field['type'], $text_types)) {
      $options[$field_name] = $instance['label'];
    }
  }
  return $options;
}

/**
 * Validation callback for actor name field.
 */
function _backdrop_federation_validate_actor_name($element, &$form_state) {
  $value = $element['#value'];
  if (!empty($value) && !preg_match('/^[a-zA-Z0-9_]+$/', $value)) {
    form_error($element, t('Actor username may only contain letters, numbers, and underscores.'));
  }
}


/**
 * Submit handler for the re-deliver button.
 *
 * Re-queues the most recent activity for each node to all current followers.
 */
function backdrop_federation_admin_redeliver_submit($form, &$form_state) {
  // Get unique inboxes from all current followers.
  $followers = db_select('backdrop_federation_followers', 'f')
    ->fields('f', array('inbox_uri', 'shared_inbox_uri'))
    ->execute();

  $inboxes = array();
  foreach ($followers as $follower) {
    $inbox = !empty($follower->shared_inbox_uri) ? $follower->shared_inbox_uri : $follower->inbox_uri;
    if (!in_array($inbox, $inboxes)) {
      $inboxes[] = $inbox;
    }
  }

  if (empty($inboxes)) {
    backdrop_set_message(t('No followers to deliver to.'), 'warning');
    return;
  }

  // Get the latest non-Delete activity ID for each node.
  $result = db_query(
    'SELECT MAX(id) AS id FROM {backdrop_federation_outbox} WHERE activity_type != :delete GROUP BY nid',
    array(':delete' => 'Delete')
  );

  $queue = BackdropQueue::get('backdrop_federation_activity_delivery');
  $queued = 0;

  foreach ($result as $row) {
    foreach ($inboxes as $inbox) {
      $queue->createItem(array(
        'activity_id' => $row->id,
        'inbox_uri' => $inbox,
      ));
      $queued++;
    }
  }

  watchdog('backdrop_federation', 'Re-queued @count deliveries via admin.', array('@count' => $queued), WATCHDOG_INFO);
  backdrop_set_message(t('Queued @count deliveries. Run cron to process them.', array('@count' => $queued)));
}

/**
 * Submit handler for admin settings form.
 */
function backdrop_federation_admin_settings_form_submit($form, &$form_state) {
  $config = config('backdrop_federation.settings');

  $config->set('enabled', (bool) $form_state['values']['enabled']);
  $config->set('actor_name', $form_state['values']['actor_name']);
  $config->set('actor_display_name', $form_state['values']['actor_display_name']);
  $config->set('actor_summary', $form_state['values']['actor_summary']);

  // Handle avatar file upload with usage tracking so the file is not
  // treated as temporary and purged by cron.
  $new_fid = !empty($form_state['values']['actor_avatar_fid']) ? (int) $form_state['values']['actor_avatar_fid'] : 0;
  $old_fid = (int) $config->get('actor_avatar_fid');
  if ($new_fid !== $old_fid) {
    if ($new_fid) {
      $file = file_load($new_fid);
      if ($file) {
        file_usage_add($file, 'backdrop_federation', 'actor', $new_fid);
      }
    }
    if ($old_fid) {
      $old_file = file_load($old_fid);
      if ($old_file) {
        file_usage_delete($old_file, 'backdrop_federation', 'actor', $old_fid);
      }
    }
  }
  $config->set('actor_avatar_fid', $new_fid);

  // Extract content types, field mapping, and object type mapping from the
  // unified per-type structure.
  $content_types = array();
  $field_mapping = array();
  $object_type_mapping = array();
  foreach ($form_state['values']['per_type'] as $type => $values) {
    if (!empty($values['enabled'])) {
      $content_types[] = $type;
    }
    // Preserve field and object_type for all types so selections survive disabling.
    if (isset($values['field'])) {
      $field_mapping[$type] = $values['field'];
    }
    if (isset($values['object_type'])) {
      $object_type_mapping[$type] = $values['object_type'];
    }
  }
  $config->set('content_types', $content_types);
  $config->set('field_mapping', $field_mapping);
  $config->set('object_type_mapping', $object_type_mapping);

  $config->set('accept_replies', (bool) $form_state['values']['accept_replies']);
  $config->set('comment_moderation', (bool) $form_state['values']['comment_moderation']);

  $config->save();

  backdrop_set_message(t('Fediverse settings saved.'));
}

/**
 * Admin form for the fediverse blocklist.
 */
function backdrop_federation_admin_blocklist_form($form, &$form_state) {
  // Current blocklist table — shown first.
  $entries = db_select('backdrop_federation_blocklist', 'b')
    ->fields('b', array('id', 'value', 'type', 'note', 'created'))
    ->orderBy('created', 'DESC')
    ->execute()
    ->fetchAll();

  if ($entries) {
    $header = array(t('Value'), t('Type'), t('Note'), t('Added'), t('Actions'));
    $rows = array();
    foreach ($entries as $entry) {
      $rows[] = array(
        check_plain($entry->value),
        check_plain($entry->type),
        check_plain($entry->note),
        format_date($entry->created, 'short'),
        l(t('Delete'), 'admin/config/services/fediverse/blocklist/delete/' . $entry->id),
      );
    }
    $form['blocklist_table'] = array(
      '#markup' => theme('table', array('header' => $header, 'rows' => $rows)),
      '#weight' => -10,
    );
  }
  else {
    $form['blocklist_table'] = array(
      '#markup' => '<p>' . t('No entries on the blocklist.') . '</p>',
      '#weight' => -10,
    );
  }

  // Add blocklist entry.
  $form['add'] = array(
    '#type' => 'fieldset',
    '#title' => t('Add to blocklist'),
  );

  $form['add']['blocklist_value'] = array(
    '#type' => 'textfield',
    '#title' => t('Actor URI or domain'),
    '#description' => t('Enter a fediverse handle (e.g. @user@example.com), a full actor URI (e.g. https://example.com/users/spammer), or a domain name (e.g. badinstance.social).'),
    '#size' => 60,
  );

  $form['add']['blocklist_type'] = array(
    '#type' => 'radios',
    '#title' => t('Type'),
    '#options' => array(
      'actor' => t('Actor URI'),
      'domain' => t('Domain'),
    ),
    '#default_value' => 'domain',
  );

  $form['add']['blocklist_note'] = array(
    '#type' => 'textfield',
    '#title' => t('Note (optional)'),
    '#size' => 60,
    '#maxlength' => 255,
  );

  $form['actions']['#type'] = 'actions';
  $form['actions']['add'] = array(
    '#type' => 'submit',
    '#value' => t('Add to blocklist'),
    '#submit' => array('backdrop_federation_admin_blocklist_add_submit'),
    '#validate' => array('backdrop_federation_admin_blocklist_add_validate'),
    '#limit_validation_errors' => array(
      array('blocklist_value'),
      array('blocklist_type'),
      array('blocklist_note'),
    ),
  );

  return $form;
}

/**
 * Admin form for fediverse rate limit settings.
 */
function backdrop_federation_admin_rate_limit_form($form, &$form_state) {
  $config = config('backdrop_federation.settings');

  $form['#config'] = 'backdrop_federation.settings';

  $form['description'] = array(
    '#markup' => '<p>' . t('Limit how many inbox requests a single actor can make within a time window. Requests that exceed the limit receive a 429 Too Many Requests response.') . '</p>',
  );

  $form['rate_limit_requests'] = array(
    '#type' => 'number',
    '#title' => t('Max requests per window'),
    '#default_value' => $config->get('rate_limit_requests') ?: 20,
    '#min' => 1,
    '#max' => 1000,
    '#required' => TRUE,
  );

  $form['rate_limit_window'] = array(
    '#type' => 'number',
    '#title' => t('Window (seconds)'),
    '#default_value' => $config->get('rate_limit_window') ?: 60,
    '#min' => 1,
    '#max' => 3600,
    '#required' => TRUE,
  );

  $form['actions']['#type'] = 'actions';
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save'),
  );

  return $form;
}

/**
 * Submit handler for the rate limit settings form.
 */
function backdrop_federation_admin_rate_limit_form_submit($form, &$form_state) {
  $config = config('backdrop_federation.settings');
  $config->set('rate_limit_requests', (int) $form_state['values']['rate_limit_requests']);
  $config->set('rate_limit_window', (int) $form_state['values']['rate_limit_window']);
  $config->save();
  backdrop_set_message(t('Rate limit settings saved.'));
}

/**
 * Validate handler for adding a blocklist entry.
 */
function backdrop_federation_admin_blocklist_add_validate($form, &$form_state) {
  $value = trim($form_state['values']['blocklist_value']);
  $type = $form_state['values']['blocklist_type'];

  if (empty($value)) {
    form_set_error('blocklist_value', t('Please enter an actor URI, domain, or fediverse handle to block.'));
    return;
  }

  // Detect @user@domain or user@domain handle format and resolve via WebFinger.
  if (preg_match('/^@?([^@\s]+)@([^@\s]+\.[^@\s]+)$/', $value, $matches)) {
    $actor_uri = _backdrop_federation_resolve_handle($matches[1], $matches[2]);
    if ($actor_uri) {
      $form_state['values']['blocklist_value'] = $actor_uri;
      $form_state['values']['blocklist_type'] = 'actor';
    }
    else {
      form_set_error('blocklist_value', t('Could not resolve @handle. Make sure the account exists and the instance is reachable.', array('@handle' => $value)));
    }
    return;
  }

  if ($type === 'actor') {
    if (!filter_var($value, FILTER_VALIDATE_URL)) {
      form_set_error('blocklist_value', t('Actor URI must be a valid URL.'));
    }
  }
  else {
    // Basic domain validation: no slashes, no spaces, has at least one dot.
    if (strpos($value, '/') !== FALSE || strpos($value, ' ') !== FALSE || strpos($value, '.') === FALSE) {
      form_set_error('blocklist_value', t('Domain must be a plain hostname like badinstance.social.'));
    }
  }
}

/**
 * Resolve a fediverse handle to an actor URI via WebFinger.
 *
 * @param string $user
 *   The username portion (without @).
 * @param string $domain
 *   The domain portion.
 *
 * @return string|false
 *   The actor URI, or FALSE if resolution failed.
 */
function _backdrop_federation_resolve_handle($user, $domain) {
  $webfinger_url = 'https://' . $domain . '/.well-known/webfinger?resource=' . urlencode('acct:' . $user . '@' . $domain);

  $response = backdrop_http_request($webfinger_url, array(
    'headers' => array('Accept' => 'application/jrd+json, application/json'),
    'timeout' => 10,
  ));

  if ($response->code != 200) {
    watchdog('backdrop_federation', 'WebFinger lookup failed for @handle: @code', array(
      '@handle' => $user . '@' . $domain,
      '@code' => $response->code,
    ), WATCHDOG_WARNING);
    return FALSE;
  }

  $data = json_decode($response->data, TRUE);
  if (empty($data['links'])) {
    return FALSE;
  }

  foreach ($data['links'] as $link) {
    if (isset($link['rel'], $link['href']) && $link['rel'] === 'self' &&
        isset($link['type']) && strpos($link['type'], 'activity+json') !== FALSE) {
      return $link['href'];
    }
  }

  return FALSE;
}

/**
 * Submit handler for adding a blocklist entry.
 */
function backdrop_federation_admin_blocklist_add_submit($form, &$form_state) {
  $value = trim($form_state['values']['blocklist_value']);
  $type = $form_state['values']['blocklist_type'];
  $note = trim($form_state['values']['blocklist_note']);

  // Check for duplicate.
  $existing = db_select('backdrop_federation_blocklist', 'b')
    ->fields('b', array('id'))
    ->condition('value', $value)
    ->condition('type', $type)
    ->execute()
    ->fetchField();

  if ($existing !== FALSE) {
    backdrop_set_message(t('@value is already on the blocklist.', array('@value' => $value)), 'warning');
    return;
  }

  db_insert('backdrop_federation_blocklist')
    ->fields(array(
      'value' => $value,
      'type' => $type,
      'note' => $note ?: NULL,
      'created' => REQUEST_TIME,
    ))
    ->execute();

  // Also remove any existing followers matching this block.
  if ($type === 'actor') {
    $deleted = db_delete('backdrop_federation_followers')
      ->condition('actor_uri', $value)
      ->execute();
    if ($deleted) {
      watchdog('backdrop_federation', 'Removed blocked actor @actor from followers.', array('@actor' => $value), WATCHDOG_INFO);
    }
  }
  else {
    // Domain block: fetch all followers and remove those on the blocked domain.
    $followers = db_select('backdrop_federation_followers', 'f')
      ->fields('f', array('id', 'actor_uri'))
      ->execute();
    foreach ($followers as $follower) {
      $follower_domain = parse_url($follower->actor_uri, PHP_URL_HOST);
      if ($follower_domain === $value) {
        db_delete('backdrop_federation_followers')
          ->condition('id', $follower->id)
          ->execute();
        watchdog('backdrop_federation', 'Removed blocked domain follower @actor.', array('@actor' => $follower->actor_uri), WATCHDOG_INFO);
      }
    }
  }

  backdrop_set_message(t('Added @value to the blocklist.', array('@value' => $value)));
  watchdog('backdrop_federation', 'Added @type @value to blocklist.', array('@type' => $type, '@value' => $value), WATCHDOG_INFO);
}

/**
 * Confirm form for deleting a blocklist entry.
 */
function backdrop_federation_admin_blocklist_delete_confirm($form, &$form_state, $id) {
  $entry = db_select('backdrop_federation_blocklist', 'b')
    ->fields('b')
    ->condition('id', $id)
    ->execute()
    ->fetchObject();

  if (!$entry) {
    backdrop_set_message(t('Blocklist entry not found.'), 'error');
    backdrop_goto('admin/config/services/fediverse/blocklist');
  }

  $form['id'] = array('#type' => 'value', '#value' => $id);
  $form['value'] = array('#type' => 'value', '#value' => $entry->value);

  return confirm_form(
    $form,
    t('Remove %value from the blocklist?', array('%value' => $entry->value)),
    'admin/config/services/fediverse/blocklist',
    t('This will allow future requests from this @type again.', array('@type' => $entry->type)),
    t('Remove'),
    t('Cancel')
  );
}

/**
 * Submit handler for blocklist delete confirm form.
 */
function backdrop_federation_admin_blocklist_delete_confirm_submit($form, &$form_state) {
  db_delete('backdrop_federation_blocklist')
    ->condition('id', $form_state['values']['id'])
    ->execute();

  backdrop_set_message(t('Removed %value from the blocklist.', array('%value' => $form_state['values']['value'])));
  $form_state['redirect'] = 'admin/config/services/fediverse/blocklist';
}

/**
 * Admin page listing outgoing activities (outbox debug view).
 */
function backdrop_federation_admin_outbox_page() {
  $total = db_select('backdrop_federation_outbox', 'o')
    ->countQuery()
    ->execute()
    ->fetchField();

  $output = '<p>' . t('The outbox records every ActivityPub activity sent from this site (Create, Update, Delete). Use the <em>View JSON</em> link to inspect the exact payload that was delivered to followers — useful for verifying activity IDs, object content, and signatures.') . '</p>';
  $output .= '<p>' . format_plural($total, '1 activity in outbox', '@count activities in outbox') . '</p>';

  if ($total == 0) {
    $output .= '<p>' . t('No outgoing activities yet.') . '</p>';
    return $output;
  }

  $rows = db_select('backdrop_federation_outbox', 'o')
    ->fields('o', array('id', 'activity_type', 'object_type', 'nid', 'activity_id', 'created'))
    ->orderBy('id', 'DESC')
    ->range(0, 50)
    ->execute();

  $header = array(t('ID'), t('Type'), t('Object'), t('Node'), t('Activity URI'), t('Created'), t('JSON'));
  $table_rows = array();

  foreach ($rows as $row) {
    $node_link = $row->nid ? l('node/' . $row->nid, 'node/' . $row->nid) : '—';
    $json_url = 'admin/config/services/fediverse/outbox/' . $row->id . '/json';
    $table_rows[] = array(
      $row->id,
      check_plain($row->activity_type),
      check_plain($row->object_type),
      $node_link,
      '<span style="font-size:0.85em;word-break:break-all;">' . check_plain($row->activity_id) . '</span>',
      format_date($row->created, 'short'),
      l(t('View JSON'), $json_url),
    );
  }

  $output .= theme('table', array('header' => $header, 'rows' => $table_rows));

  if ($total > 50) {
    $output .= '<p>' . t('Showing most recent 50 of @total activities.', array('@total' => $total)) . '</p>';
  }

  return $output;
}

/**
 * Renders raw JSON for a single outbox activity.
 *
 * @param int $id
 *   The outbox row ID.
 */
function backdrop_federation_admin_outbox_json($id) {
  $row = db_select('backdrop_federation_outbox', 'o')
    ->fields('o', array('id', 'activity_type', 'nid', 'activity_json', 'created'))
    ->condition('id', (int) $id)
    ->execute()
    ->fetchObject();

  if (!$row) {
    return MENU_NOT_FOUND;
  }

  // Pretty-print the stored JSON.
  $decoded = json_decode($row->activity_json);
  $pretty = json_encode($decoded, JSON_UNESCAPED_SLASHES | JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE);

  $back_link = l(t('← Back to Outbox'), 'admin/config/services/fediverse/outbox');
  $meta = t('Activity #@id &middot; @type &middot; @date', array(
    '@id' => $row->id,
    '@type' => $row->activity_type,
    '@date' => format_date($row->created, 'medium'),
  ));

  $output  = '<p>' . $back_link . '</p>';
  $output .= '<p>' . $meta . '</p>';
  $output .= '<pre style="background:#f5f5f5;padding:1em;overflow:auto;font-size:0.85em;">';
  $output .= check_plain($pretty);
  $output .= '</pre>';

  return $output;
}

/**
 * Admin page listing all fediverse followers.
 */
function backdrop_federation_admin_followers_page() {
  $follower_count = db_select('backdrop_federation_followers', 'f')
    ->countQuery()
    ->execute()
    ->fetchField();

  $output = '<p>' . format_plural($follower_count, '1 follower', '@count followers') . '</p>';

  if ($follower_count == 0) {
    $output .= '<p>' . t('No followers yet.') . '</p>';
    return $output;
  }

  $followers = db_select('backdrop_federation_followers', 'f')
    ->fields('f', array('actor_uri', 'created'))
    ->orderBy('created', 'DESC')
    ->execute();

  $header = array(t('Actor'), t('Followed'));
  $rows = array();

  foreach ($followers as $follower) {
    $rows[] = array(
      l($follower->actor_uri, $follower->actor_uri),
      format_date($follower->created, 'short'),
    );
  }

  $output .= theme('table', array('header' => $header, 'rows' => $rows));

  return $output;
}
