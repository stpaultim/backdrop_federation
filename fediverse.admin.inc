<?php
/**
 * @file
 * Admin configuration form for Fediverse module.
 */

/**
 * Admin settings form.
 */
function fediverse_admin_settings_form($form, &$form_state) {
  $config = config('fediverse.settings');

  $form['#config'] = 'fediverse.settings';

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
    '#element_validate' => array('_fediverse_validate_actor_name'),
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

  // Content types.
  $form['content'] = array(
    '#type' => 'fieldset',
    '#title' => t('Content federation'),
    '#collapsible' => TRUE,
  );

  $node_types = node_type_get_names();
  $form['content']['content_types'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Content types to federate'),
    '#description' => t('Select which content types should be published to followers when created or updated.'),
    '#options' => $node_types,
    '#default_value' => $config->get('content_types') ?: array(),
  );

  // Per-content-type field mapping.
  $field_mapping = $config->get('field_mapping') ?: array();

  $form['content']['field_mapping'] = array(
    '#type' => 'container',
    '#tree' => TRUE,
  );

  foreach ($node_types as $type => $name) {
    $text_fields = _fediverse_get_text_fields($type);
    if (empty($text_fields)) {
      continue;
    }
    $default = isset($field_mapping[$type]) ? $field_mapping[$type] : (isset($text_fields['body']) ? 'body' : key($text_fields));
    $form['content']['field_mapping'][$type] = array(
      '#type' => 'select',
      '#title' => t('Content field for %type', array('%type' => $name)),
      '#description' => t('Field used as the post body when federating this content type.'),
      '#options' => $text_fields,
      '#default_value' => $default,
      '#states' => array(
        'visible' => array(
          ':input[name="content_types[' . $type . ']"]' => array('checked' => TRUE),
        ),
      ),
    );
  }

  // Status information.
  $form['status'] = array(
    '#type' => 'fieldset',
    '#title' => t('Status'),
    '#collapsible' => TRUE,
  );

  // Follower count.
  $follower_count = db_select('fediverse_followers', 'f')
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
  $keypair = fediverse_get_keypair();
  $form['status']['keypair_status'] = array(
    '#type' => 'item',
    '#title' => t('Keypair'),
    '#markup' => $keypair ? t('Generated and ready') : t('<span class="error">Missing - try reinstalling module</span>'),
  );

  // Actor URL.
  $form['status']['actor_url'] = array(
    '#type' => 'item',
    '#title' => t('Actor URL'),
    '#markup' => l(fediverse_get_actor_uri(), 'fediverse/actor'),
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

  $outbox_count = db_select('fediverse_outbox', 'o')
    ->countQuery()
    ->execute()
    ->fetchField();

  $form['maintenance']['redeliver_description'] = array(
    '#markup' => '<p>' . t('Re-queue all posts in the outbox for delivery to current followers. Useful if posts were published before any followers existed. Run cron after clicking to process the queue.') . '</p>',
  );

  $form['maintenance']['redeliver'] = array(
    '#type' => 'submit',
    '#value' => t('Re-deliver all posts (@count)', array('@count' => $outbox_count)),
    '#submit' => array('fediverse_admin_redeliver_submit'),
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
function _fediverse_get_text_fields($node_type) {
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
function _fediverse_validate_actor_name($element, &$form_state) {
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
function fediverse_admin_redeliver_submit($form, &$form_state) {
  // Get unique inboxes from all current followers.
  $followers = db_select('fediverse_followers', 'f')
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
    'SELECT MAX(id) AS id FROM {fediverse_outbox} WHERE activity_type != :delete GROUP BY nid',
    array(':delete' => 'Delete')
  );

  $queue = BackdropQueue::get('fediverse_activity_delivery');
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

  watchdog('fediverse', 'Re-queued @count deliveries via admin.', array('@count' => $queued), WATCHDOG_INFO);
  backdrop_set_message(t('Queued @count deliveries. Run cron to process them.', array('@count' => $queued)));
}

/**
 * Submit handler for admin settings form.
 */
function fediverse_admin_settings_form_submit($form, &$form_state) {
  $config = config('fediverse.settings');

  $config->set('enabled', (bool) $form_state['values']['enabled']);
  $config->set('actor_name', $form_state['values']['actor_name']);
  $config->set('actor_display_name', $form_state['values']['actor_display_name']);
  $config->set('actor_summary', $form_state['values']['actor_summary']);

  // Filter out unchecked content types.
  $content_types = array_filter($form_state['values']['content_types']);
  $config->set('content_types', array_values($content_types));

  // Save field mapping (preserve all types so selection survives disabling).
  $config->set('field_mapping', $form_state['values']['field_mapping'] ?: array());

  $config->save();

  backdrop_set_message(t('Fediverse settings saved.'));
}

/**
 * Admin page listing all fediverse followers.
 */
function fediverse_admin_followers_page() {
  $follower_count = db_select('fediverse_followers', 'f')
    ->countQuery()
    ->execute()
    ->fetchField();

  $output = '<p>' . l(t('Back to Fediverse settings'), 'admin/config/services/fediverse') . '</p>';
  $output .= '<p>' . format_plural($follower_count, '1 follower', '@count followers') . '</p>';

  if ($follower_count == 0) {
    $output .= '<p>' . t('No followers yet.') . '</p>';
    return $output;
  }

  $followers = db_select('fediverse_followers', 'f')
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
