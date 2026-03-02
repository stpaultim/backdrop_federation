<?php
/**
 * @file
 * Admin pages for Backdrop Federation Follow sub-module.
 */

/**
 * Admin form for managing followed accounts.
 */
function backdrop_federation_follow_admin_form($form, &$form_state) {
  $form['#attached']['css'][] = array(
    'data' => '.backdrop-federation-status-pending  { color: #e6a817; font-weight: bold; }
               .backdrop-federation-status-accepted { color: #2e7d32; font-weight: bold; }
               .backdrop-federation-status-rejected { color: #c62828; font-weight: bold; }
               .backdrop-federation-status-paused   { color: #757575; font-weight: bold; }',
    'type' => 'inline',
  );

  $form['help'] = array(
    '#markup' => '<p>' . t('Follow other Fediverse accounts to receive their posts in your local <a href="!feed">feed</a>. Enter a handle below to send a Follow request — their server will send an Accept or Reject back to your inbox.', array('!feed' => url('admin/config/services/fediverse/feed'))) . '</p>',
    '#weight' => -20,
  );

  // Current following table.
  $following = db_select('backdrop_federation_following', 'f')
    ->fields('f')
    ->orderBy('created', 'DESC')
    ->execute()
    ->fetchAll();

  if ($following) {
    // Show "Pause All" link only when at least one account is active.
    $active_count = 0;
    foreach ($following as $account) {
      if (in_array($account->status, array('pending', 'accepted'))) {
        $active_count++;
      }
    }
    if ($active_count) {
      $form['pause_all_link'] = array(
        '#markup' => '<p style="text-align: right">' . l(t('Pause all active follows'), 'admin/config/services/fediverse/following/pause-all') . '</p>',
        '#weight' => -9,
      );
    }

    $header = array(t('Account'), t('Display name'), t('Status'), t('Since'), t('Actions'));
    $rows = array();
    foreach ($following as $account) {
      $status_labels = array(
        'pending'  => '<span class="backdrop-federation-status-pending">'  . t('Pending')  . '</span>',
        'accepted' => '<span class="backdrop-federation-status-accepted">' . t('Accepted') . '</span>',
        'rejected' => '<span class="backdrop-federation-status-rejected">' . t('Rejected') . '</span>',
        'paused'   => '<span class="backdrop-federation-status-paused">'   . t('Paused')   . '</span>',
      );
      $status = isset($status_labels[$account->status]) ? $status_labels[$account->status] : check_plain($account->status);
      $handle = '@' . $account->username . '@' . $account->domain;

      $actions = array();
      if (in_array($account->status, array('pending', 'accepted'))) {
        $actions[] = l(t('Pause'), 'admin/config/services/fediverse/following/pause/' . $account->id);
      }
      if ($account->status === 'paused') {
        $actions[] = l(t('Resume'), 'admin/config/services/fediverse/following/resume/' . $account->id);
      }
      $actions[] = l(t('Unfollow'), 'admin/config/services/fediverse/following/unfollow/' . $account->id);

      $rows[] = array(
        l($handle, $account->actor_uri),
        check_plain($account->display_name),
        $status,
        format_date($account->created, 'short'),
        implode(' | ', $actions),
      );
    }
    $form['following_table'] = array(
      '#markup' => theme('table', array('header' => $header, 'rows' => $rows)),
      '#weight' => -10,
    );
  }
  else {
    $form['following_table'] = array(
      '#markup' => '<p>' . t('Not following any accounts yet.') . '</p>',
      '#weight' => -10,
    );
  }

  // Follow new account fieldset.
  $form['follow'] = array(
    '#type' => 'fieldset',
    '#title' => t('Follow an account'),
  );

  $form['follow']['handle'] = array(
    '#type' => 'textfield',
    '#title' => t('Fediverse handle'),
    '#description' => t('Enter a handle like @user@mastodon.social. A signed Follow request will be sent immediately.'),
    '#placeholder' => '@user@mastodon.social',
    '#size' => 40,
    '#maxlength' => 255,
  );

  $form['follow']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Send Follow request'),
    '#validate' => array('backdrop_federation_follow_admin_form_validate'),
    '#submit' => array('backdrop_federation_follow_admin_form_submit'),
    '#limit_validation_errors' => array(array('handle')),
  );

  // Feed retention settings.
  $form['settings'] = array(
    '#type' => 'fieldset',
    '#title' => t('Feed settings'),
  );

  $form['settings']['include_boosts'] = array(
    '#type' => 'checkbox',
    '#title' => t('Include boosts (reposts)'),
    '#description' => t('When enabled, posts boosted by followed accounts will also appear in the feed, labelled "Boosted by @handle".'),
    '#default_value' => config_get('backdrop_federation_follow.settings', 'include_boosts'),
  );

  $form['settings']['feed_retention'] = array(
    '#type' => 'select',
    '#title' => t('Delete feed posts after'),
    '#description' => t('Incoming posts from followed accounts will be automatically removed after this period. Deletion runs during cron.'),
    '#options' => array(
      '0'         => t('Never (keep forever)'),
      '300'       => t('5 minutes (testing only)'),
      '3600'      => t('1 hour (testing only)'),
      '604800'    => t('1 week'),
      '2592000'   => t('1 month'),
      '15552000'  => t('6 months'),
      '31536000'  => t('1 year'),
      '157680000' => t('5 years'),
    ),
    '#default_value' => config_get('backdrop_federation_follow.settings', 'feed_retention'),
  );

  $form['settings']['save'] = array(
    '#type' => 'submit',
    '#value' => t('Save settings'),
    '#submit' => array('backdrop_federation_follow_settings_submit'),
    '#limit_validation_errors' => array(array('feed_retention'), array('include_boosts')),
  );

  return $form;
}

/**
 * Validate handler for the follow form.
 */
function backdrop_federation_follow_admin_form_validate($form, &$form_state) {
  $handle = ltrim(trim($form_state['values']['handle']), '@');
  if (!preg_match('/^[^@\s]+@[^@\s]+\.[^@\s]+$/', $handle)) {
    form_set_error('handle', t('Please enter a valid Fediverse handle, e.g. @user@mastodon.social.'));
  }
}

/**
 * Submit handler for the follow form.
 */
function backdrop_federation_follow_admin_form_submit($form, &$form_state) {
  $handle = ltrim(trim($form_state['values']['handle']), '@');

  $resolved = _backdrop_federation_follow_resolve_handle($handle);
  if (!$resolved) {
    backdrop_set_message(t('Could not resolve @handle. Make sure the account exists and the instance is reachable.', array('@handle' => '@' . $handle)), 'error');
    return;
  }

  // Check whether we are already following this account.
  $existing = db_select('backdrop_federation_following', 'f')
    ->fields('f', array('id', 'status'))
    ->condition('actor_uri', $resolved['actor_uri'])
    ->execute()
    ->fetchObject();

  if ($existing) {
    backdrop_set_message(t('Already following @handle (status: @status).', array(
      '@handle' => '@' . $handle,
      '@status' => $existing->status,
    )), 'warning');
    return;
  }

  // Send the signed Follow activity.
  $activity_id = _backdrop_federation_follow_send_follow($resolved['actor_uri'], $resolved['inbox_uri']);
  if (!$activity_id) {
    backdrop_set_message(t('Failed to send Follow request to @handle. Check the watchdog log for details.', array('@handle' => '@' . $handle)), 'error');
    return;
  }

  db_insert('backdrop_federation_following')
    ->fields(array(
      'actor_uri'        => $resolved['actor_uri'],
      'inbox_uri'        => $resolved['inbox_uri'],
      'username'         => _backdrop_federation_follow_strip_emoji($resolved['username']),
      'domain'           => $resolved['domain'],
      'display_name'     => _backdrop_federation_follow_strip_emoji($resolved['display_name']),
      'avatar_url'       => $resolved['avatar_url'],
      'status'           => 'pending',
      'follow_activity_id' => $activity_id,
      'created'          => REQUEST_TIME,
    ))
    ->execute();

  backdrop_set_message(t('Follow request sent to @handle. The status will update to "Accepted" once their server responds.', array('@handle' => '@' . $handle)));
  watchdog('backdrop_federation_follow', 'Sent Follow request to @actor', array('@actor' => $resolved['actor_uri']), WATCHDOG_INFO);
}

/**
 * Submit handler for feed retention settings.
 */
function backdrop_federation_follow_settings_submit($form, &$form_state) {
  $retention = $form_state['values']['feed_retention'];

  config('backdrop_federation_follow.settings')
    ->set('feed_retention', $retention)
    ->set('include_boosts', (bool) $form_state['values']['include_boosts'])
    ->save();

  // Apply the new retention period immediately so posts that now fall outside
  // the window are removed without waiting for the next cron run.
  $deleted = _backdrop_federation_follow_expire_feed_posts($retention);
  if ($deleted) {
    backdrop_set_message(t('Feed settings saved. Removed @count posts that exceeded the new retention period.', array('@count' => $deleted)));
  }
  else {
    backdrop_set_message(t('Feed settings saved.'));
  }
}

/**
 * Confirm form for unfollowing an account.
 *
 * @param int $id
 *   The backdrop_federation_following row ID.
 */
function backdrop_federation_follow_unfollow_confirm($form, &$form_state, $id) {
  $account = db_select('backdrop_federation_following', 'f')
    ->fields('f')
    ->condition('id', (int) $id)
    ->execute()
    ->fetchObject();

  if (!$account) {
    backdrop_set_message(t('Account not found.'), 'error');
    backdrop_goto('admin/config/services/fediverse/following');
  }

  $form['id']               = array('#type' => 'value', '#value' => (int) $id);
  $form['actor_uri']        = array('#type' => 'value', '#value' => $account->actor_uri);
  $form['inbox_uri']        = array('#type' => 'value', '#value' => $account->inbox_uri);
  $form['follow_activity_id'] = array('#type' => 'value', '#value' => $account->follow_activity_id);
  $form['handle']           = array('#type' => 'value', '#value' => '@' . $account->username . '@' . $account->domain);

  return confirm_form(
    $form,
    t('Unfollow %handle?', array('%handle' => '@' . $account->username . '@' . $account->domain)),
    'admin/config/services/fediverse/following',
    t('This will send an Undo Follow activity to their server and remove all their posts from your local feed.'),
    t('Unfollow'),
    t('Cancel')
  );
}

/**
 * Submit handler for the unfollow confirm form.
 */
function backdrop_federation_follow_unfollow_confirm_submit($form, &$form_state) {
  _backdrop_federation_follow_send_unfollow(
    $form_state['values']['actor_uri'],
    $form_state['values']['inbox_uri'],
    $form_state['values']['follow_activity_id']
  );

  db_delete('backdrop_federation_following')
    ->condition('id', $form_state['values']['id'])
    ->execute();

  // Remove their posts from the local feed.
  db_delete('backdrop_federation_feed')
    ->condition('actor_uri', $form_state['values']['actor_uri'])
    ->execute();

  backdrop_set_message(t('Unfollowed @handle.', array('@handle' => $form_state['values']['handle'])));
  watchdog('backdrop_federation_follow', 'Unfollowed @actor', array('@actor' => $form_state['values']['actor_uri']), WATCHDOG_INFO);
  $form_state['redirect'] = 'admin/config/services/fediverse/following';
}

/**
 * Confirm form for pausing a followed account.
 *
 * Sends an Undo Follow so the remote server stops delivering posts, but keeps
 * the record so the user can resume with one click later.
 *
 * @param int $id
 *   The backdrop_federation_following row ID.
 */
function backdrop_federation_follow_pause_confirm($form, &$form_state, $id) {
  $account = db_select('backdrop_federation_following', 'f')
    ->fields('f')
    ->condition('id', (int) $id)
    ->execute()
    ->fetchObject();

  if (!$account) {
    backdrop_set_message(t('Account not found.'), 'error');
    backdrop_goto('admin/config/services/fediverse/following');
  }

  $form['id']                 = array('#type' => 'value', '#value' => (int) $id);
  $form['actor_uri']          = array('#type' => 'value', '#value' => $account->actor_uri);
  $form['inbox_uri']          = array('#type' => 'value', '#value' => $account->inbox_uri);
  $form['follow_activity_id'] = array('#type' => 'value', '#value' => $account->follow_activity_id);
  $form['handle']             = array('#type' => 'value', '#value' => '@' . $account->username . '@' . $account->domain);

  return confirm_form(
    $form,
    t('Pause following %handle?', array('%handle' => '@' . $account->username . '@' . $account->domain)),
    'admin/config/services/fediverse/following',
    t('This will send an Undo Follow to their server so they stop delivering posts to this site. The account will remain listed so you can resume following it later.'),
    t('Pause'),
    t('Cancel')
  );
}

/**
 * Submit handler for the pause confirm form.
 */
function backdrop_federation_follow_pause_confirm_submit($form, &$form_state) {
  _backdrop_federation_follow_send_unfollow(
    $form_state['values']['actor_uri'],
    $form_state['values']['inbox_uri'],
    $form_state['values']['follow_activity_id']
  );

  db_update('backdrop_federation_following')
    ->fields(array('status' => 'paused'))
    ->condition('id', $form_state['values']['id'])
    ->execute();

  backdrop_set_message(t('Paused @handle. Their account remains listed so you can resume following it later.', array('@handle' => $form_state['values']['handle'])));
  watchdog('backdrop_federation_follow', 'Paused following @actor', array('@actor' => $form_state['values']['actor_uri']), WATCHDOG_INFO);
  $form_state['redirect'] = 'admin/config/services/fediverse/following';
}

/**
 * Confirm form for resuming a paused account.
 *
 * @param int $id
 *   The backdrop_federation_following row ID.
 */
function backdrop_federation_follow_resume_confirm($form, &$form_state, $id) {
  $account = db_select('backdrop_federation_following', 'f')
    ->fields('f')
    ->condition('id', (int) $id)
    ->execute()
    ->fetchObject();

  if (!$account) {
    backdrop_set_message(t('Account not found.'), 'error');
    backdrop_goto('admin/config/services/fediverse/following');
  }

  $form['id']        = array('#type' => 'value', '#value' => (int) $id);
  $form['actor_uri'] = array('#type' => 'value', '#value' => $account->actor_uri);
  $form['inbox_uri'] = array('#type' => 'value', '#value' => $account->inbox_uri);
  $form['handle']    = array('#type' => 'value', '#value' => '@' . $account->username . '@' . $account->domain);

  return confirm_form(
    $form,
    t('Resume following %handle?', array('%handle' => '@' . $account->username . '@' . $account->domain)),
    'admin/config/services/fediverse/following',
    t('This will send a new Follow request to their server. Posts will start appearing in your feed once they accept.'),
    t('Resume'),
    t('Cancel')
  );
}

/**
 * Submit handler for the resume confirm form.
 */
function backdrop_federation_follow_resume_confirm_submit($form, &$form_state) {
  $actor_uri = $form_state['values']['actor_uri'];
  $inbox_uri = $form_state['values']['inbox_uri'];
  $handle    = $form_state['values']['handle'];

  $activity_id = _backdrop_federation_follow_send_follow($actor_uri, $inbox_uri);
  if (!$activity_id) {
    backdrop_set_message(t('Failed to send Follow request to @handle. Check the watchdog log for details.', array('@handle' => $handle)), 'error');
    return;
  }

  db_update('backdrop_federation_following')
    ->fields(array(
      'status'             => 'pending',
      'follow_activity_id' => $activity_id,
    ))
    ->condition('id', $form_state['values']['id'])
    ->execute();

  backdrop_set_message(t('Follow request resent to @handle. Status will update to "Accepted" once their server responds.', array('@handle' => $handle)));
  watchdog('backdrop_federation_follow', 'Resumed following @actor', array('@actor' => $actor_uri), WATCHDOG_INFO);
  $form_state['redirect'] = 'admin/config/services/fediverse/following';
}

/**
 * Confirm form for pausing all active followed accounts.
 *
 * Sends Undo Follow to every pending or accepted account and marks them all
 * as paused, so they can be resumed individually later.
 */
function backdrop_federation_follow_pause_all_confirm($form, &$form_state) {
  $count = db_select('backdrop_federation_following', 'f')
    ->condition('status', array('pending', 'accepted'), 'IN')
    ->countQuery()
    ->execute()
    ->fetchField();

  if (!$count) {
    backdrop_set_message(t('No active follows to pause.'), 'warning');
    backdrop_goto('admin/config/services/fediverse/following');
  }

  return confirm_form(
    $form,
    t('Pause all active follows?'),
    'admin/config/services/fediverse/following',
    format_plural(
      $count,
      'This will send an Undo Follow to 1 account and stop their posts from being delivered to this site. All accounts will remain listed so you can resume them individually later.',
      'This will send an Undo Follow to @count accounts and stop their posts from being delivered to this site. All accounts will remain listed so you can resume them individually later.'
    ),
    t('Pause all'),
    t('Cancel')
  );
}

/**
 * Submit handler for the pause-all confirm form.
 */
function backdrop_federation_follow_pause_all_confirm_submit($form, &$form_state) {
  $accounts = db_select('backdrop_federation_following', 'f')
    ->fields('f')
    ->condition('status', array('pending', 'accepted'), 'IN')
    ->execute()
    ->fetchAll();

  $paused = 0;
  $failed = 0;

  foreach ($accounts as $account) {
    $result = _backdrop_federation_follow_send_unfollow(
      $account->actor_uri,
      $account->inbox_uri,
      $account->follow_activity_id
    );

    db_update('backdrop_federation_following')
      ->fields(array('status' => 'paused'))
      ->condition('id', $account->id)
      ->execute();

    if ($result) {
      $paused++;
    }
    else {
      $failed++;
    }
  }

  if ($paused) {
    backdrop_set_message(format_plural(
      $paused,
      'Paused 1 account. Their accounts remain listed so you can resume them individually later.',
      'Paused @count accounts. Their accounts remain listed so you can resume them individually later.'
    ));
  }
  if ($failed) {
    backdrop_set_message(format_plural(
      $failed,
      '1 Undo Follow could not be delivered (check the log), but the account has been paused locally.',
      '@count Undo Follow activities could not be delivered (check the log), but those accounts have been paused locally.'
    ), 'warning');
  }

  watchdog('backdrop_federation_follow', 'Paused all active follows: @paused paused, @failed delivery failures.', array(
    '@paused' => $paused,
    '@failed' => $failed,
  ), WATCHDOG_INFO);

  $form_state['redirect'] = 'admin/config/services/fediverse/following';
}

/**
 * Admin feed page — lists posts received from followed accounts.
 */
function backdrop_federation_follow_admin_feed_page() {
  $total = db_select('backdrop_federation_feed', 'f')
    ->countQuery()
    ->execute()
    ->fetchField();

  $output  = '<p>' . t('Posts received from accounts this site follows, stored in your local feed. Only accounts with an "Accepted" follow status will appear here.') . '</p>';
  $output .= '<p>' . format_plural($total, '1 post in feed', '@count posts in feed') . '</p>';

  if ($total == 0) {
    $output .= '<p>' . t('No posts yet. <a href="!url">Follow some accounts</a> and wait for them to post.', array('!url' => url('admin/config/services/fediverse/following'))) . '</p>';
    return $output;
  }

  $rows = db_select('backdrop_federation_feed', 'f')
    ->extend('PagerDefault')
    ->fields('f')
    ->orderBy('published', 'DESC')
    ->limit(20)
    ->execute()
    ->fetchAll();

  // Pre-fetch all account info in one query to avoid N+1 queries in the loop.
  $actor_uris = array_unique(array_column((array) $rows, 'actor_uri'));
  $accounts = array();
  if (!empty($actor_uris)) {
    $result = db_select('backdrop_federation_following', 'f')
      ->fields('f', array('actor_uri', 'username', 'domain', 'display_name', 'avatar_url'))
      ->condition('actor_uri', $actor_uris, 'IN')
      ->execute();
    foreach ($result as $row) {
      $accounts[$row->actor_uri] = $row;
    }
  }

  foreach ($rows as $row) {
    $account = isset($accounts[$row->actor_uri]) ? $accounts[$row->actor_uri] : NULL;

    $handle = $account
      ? ('@' . check_plain($account->username) . '@' . check_plain($account->domain))
      : check_plain($row->actor_uri);
    $display_name = $account ? check_plain($account->display_name) : check_plain($row->actor_uri);

    $object = json_decode($row->object_json, TRUE);
    $original_url = _backdrop_federation_follow_extract_url(
      isset($object['url']) ? $object['url'] : (isset($object['id']) ? $object['id'] : NULL)
    );

    $boost_line = '';
    if ($row->activity_type === 'Announce') {
      $boost_line = '<div style="font-size:0.85em;color:#666;margin-bottom:0.25em;">&#x21BA; '
        . t('Boosted by !handle', array('!handle' => '<strong>' . $handle . '</strong>'))
        . '</div>';
      // For boosts, try to show the original author from the stored object.
      $object_data    = json_decode($row->object_json, TRUE);
      $attributed_uri = isset($object_data['attributedTo']) ? $object_data['attributedTo'] : NULL;
      if ($attributed_uri) {
        $display_name = check_plain($attributed_uri);
        $handle = '';
      }
    }

    $meta = $boost_line
      . '<strong>' . $display_name . '</strong>'
      . ($handle ? ' <span style="color:#666;">' . $handle . '</span>' : '')
      . ' &middot; ' . format_date($row->published, 'short');

    if ($original_url) {
      $meta .= ' &middot; ' . l(t('View original'), $original_url);
    }

    $output .= '<div style="border-bottom:1px solid #ddd;padding:1em 0;">';
    $output .= '<p style="margin:0 0 0.5em;">' . $meta . '</p>';
    $output .= '<div>' . $row->content . '</div>';
    $output .= _backdrop_federation_follow_render_attachments($object);
    $output .= '</div>';
  }

  $output .= theme('pager');

  return $output;
}

/**
 * Render media attachments from an ActivityPub object.
 *
 * Images are displayed inline. Other document types (audio, video, etc.)
 * are rendered as labelled links to the original file.
 *
 * @param array $object
 *   The decoded ActivityPub object array.
 *
 * @return string
 *   HTML string of rendered attachments, or empty string if none.
 */
function _backdrop_federation_follow_render_attachments($object) {
  if (empty($object['attachment']) || !is_array($object['attachment'])) {
    return '';
  }

  $output = '';

  foreach ($object['attachment'] as $attachment) {
    $url = isset($attachment['url']) ? $attachment['url'] : NULL;
    if (!$url || !filter_var($url, FILTER_VALIDATE_URL)) {
      continue;
    }

    $media_type = isset($attachment['mediaType']) ? $attachment['mediaType'] : '';
    $alt = isset($attachment['name']) ? check_plain($attachment['name']) : '';
    $safe_url = check_url($url);

    if (strpos($media_type, 'image/') === 0 || $attachment['type'] === 'Image') {
      $output .= '<img src="' . $safe_url . '" alt="' . $alt . '"'
        . ' style="max-width:200px;max-height:200px;width:auto;height:auto;display:block;margin-top:0.5em;">';
    }
    elseif (strpos($media_type, 'video/') === 0) {
      $output .= '<video controls style="max-width:100%;display:block;margin-top:0.5em;">'
        . '<source src="' . $safe_url . '" type="' . check_plain($media_type) . '">'
        . '<a href="' . $safe_url . '">' . ($alt ?: t('Download video')) . '</a>'
        . '</video>';
    }
    elseif (strpos($media_type, 'audio/') === 0) {
      $output .= '<audio controls style="width:100%;margin-top:0.5em;">'
        . '<source src="' . $safe_url . '" type="' . check_plain($media_type) . '">'
        . '<a href="' . $safe_url . '">' . ($alt ?: t('Download audio')) . '</a>'
        . '</audio>';
    }
    else {
      // Unknown type — render as a download link.
      $label = $alt ?: check_plain(basename(parse_url($url, PHP_URL_PATH)));
      $output .= '<p><a href="' . $safe_url . '">' . $label . '</a></p>';
    }
  }

  return $output;
}
