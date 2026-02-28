<?php
/**
 * @file
 * Views integration for Backdrop Federation Follow.
 *
 * Exposes backdrop_federation_feed as a Views base table with a relationship
 * to backdrop_federation_following, so site builders can create custom feeds,
 * public pages, and blocks without writing code.
 */

/**
 * Custom field handler that renders pre-sanitized HTML content directly.
 *
 * Feed post content is already passed through filter_xss() at ingest time.
 * The default views_handler_field would call check_plain() and escape the
 * markup, so we override render() to return the stored value as-is.
 */
class backdrop_federation_follow_handler_field_html extends views_handler_field {
  function render($values) {
    return $this->get_value($values);
  }
}

/**
 * Implements hook_views_data().
 */
function backdrop_federation_follow_views_data() {
  $data = array();

  // ---------------------------------------------------------------------------
  // backdrop_federation_feed — base table.
  // ---------------------------------------------------------------------------

  $data['backdrop_federation_feed']['table']['group'] = t('Fediverse Feed');

  $data['backdrop_federation_feed']['table']['base'] = array(
    'field' => 'id',
    'title' => t('Fediverse Feed Post'),
    'help' => t('Posts received from Fediverse accounts this site follows.'),
  );

  // Implicit join so backdrop_federation_following can be used as a secondary
  // table when backdrop_federation_feed is the base.
  $data['backdrop_federation_feed']['table']['join'] = array(
    'backdrop_federation_following' => array(
      'left_field' => 'actor_uri',
      'field' => 'actor_uri',
    ),
  );

  // id.
  $data['backdrop_federation_feed']['id'] = array(
    'title' => t('Feed post ID'),
    'help' => t('Internal numeric ID of the feed post.'),
    'field' => array(
      'handler' => 'views_handler_field_numeric',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_numeric',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_numeric',
    ),
  );

  // actor_uri — also defines the relationship to backdrop_federation_following.
  $data['backdrop_federation_feed']['actor_uri'] = array(
    'title' => t('Actor URI'),
    'help' => t('ActivityPub URI of the account that created or boosted this post.'),
    'field' => array(
      'handler' => 'views_handler_field_url',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
    'relationship' => array(
      'title' => t('Followed account'),
      'help' => t('Relate this feed post to the account record in the following list.'),
      'base' => 'backdrop_federation_following',
      'base field' => 'actor_uri',
      'handler' => 'views_handler_relationship',
      'label' => t('Followed account'),
    ),
  );

  // object_id — canonical URL of the original post on the remote server.
  $data['backdrop_federation_feed']['object_id'] = array(
    'title' => t('Post URL'),
    'help' => t('The canonical URL of the original post on the remote server. Enable "Output this field as a link" to make it clickable.'),
    'field' => array(
      'handler' => 'views_handler_field_url',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // object_type — Note or Article.
  $data['backdrop_federation_feed']['object_type'] = array(
    'title' => t('Object type'),
    'help' => t('ActivityPub object type: Note or Article.'),
    'field' => array(
      'handler' => 'views_handler_field',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_in_operator',
      'options callback' => '_backdrop_federation_follow_views_object_types',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // activity_type — Create (original post) or Announce (boost).
  $data['backdrop_federation_feed']['activity_type'] = array(
    'title' => t('Activity type'),
    'help' => t('Create = original post; Announce = boost from a followed account.'),
    'field' => array(
      'handler' => 'views_handler_field',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_in_operator',
      'options callback' => '_backdrop_federation_follow_views_activity_types',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // content — pre-sanitized HTML; uses custom handler to avoid double-escaping.
  $data['backdrop_federation_feed']['content'] = array(
    'title' => t('Content'),
    'help' => t('Sanitized HTML content of the post. Already passed through filter_xss() at ingest time.'),
    'field' => array(
      'handler' => 'backdrop_federation_follow_handler_field_html',
      'click sortable' => FALSE,
    ),
  );

  // published — timestamp from the remote server.
  $data['backdrop_federation_feed']['published'] = array(
    'title' => t('Published date'),
    'help' => t('The date the post was published on the remote server.'),
    'field' => array(
      'handler' => 'views_handler_field_date',
      'click sortable' => TRUE,
    ),
    'sort' => array(
      'handler' => 'views_handler_sort_date',
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_date',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_date',
    ),
  );

  // received — timestamp when this site ingested the post.
  $data['backdrop_federation_feed']['received'] = array(
    'title' => t('Received date'),
    'help' => t('The date this site received and stored the post.'),
    'field' => array(
      'handler' => 'views_handler_field_date',
      'click sortable' => TRUE,
    ),
    'sort' => array(
      'handler' => 'views_handler_sort_date',
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_date',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_date',
    ),
  );

  // ---------------------------------------------------------------------------
  // backdrop_federation_following — secondary table, also a standalone base.
  // ---------------------------------------------------------------------------

  $data['backdrop_federation_following']['table']['group'] = t('Fediverse Following');

  $data['backdrop_federation_following']['table']['base'] = array(
    'field' => 'id',
    'title' => t('Fediverse Followed Account'),
    'help' => t('Fediverse accounts this site follows.'),
  );

  // Implicit join back to backdrop_federation_feed.
  $data['backdrop_federation_following']['table']['join'] = array(
    'backdrop_federation_feed' => array(
      'left_field' => 'actor_uri',
      'field' => 'actor_uri',
    ),
  );

  // id.
  $data['backdrop_federation_following']['id'] = array(
    'title' => t('Following record ID'),
    'help' => t('Internal numeric ID of the following record.'),
    'field' => array(
      'handler' => 'views_handler_field_numeric',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_numeric',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_numeric',
    ),
  );

  // actor_uri — also defines the reverse relationship to backdrop_federation_feed.
  $data['backdrop_federation_following']['actor_uri'] = array(
    'title' => t('Actor URI'),
    'help' => t('The full ActivityPub URI of the followed account.'),
    'field' => array(
      'handler' => 'views_handler_field_url',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
    'relationship' => array(
      'title' => t('Feed posts'),
      'help' => t('Relate this followed account to its received feed posts.'),
      'base' => 'backdrop_federation_feed',
      'base field' => 'actor_uri',
      'handler' => 'views_handler_relationship',
      'label' => t('Feed posts'),
    ),
  );

  // username.
  $data['backdrop_federation_following']['username'] = array(
    'title' => t('Username'),
    'help' => t("The account username without the @ prefix or domain (e.g. alice)."),
    'field' => array(
      'handler' => 'views_handler_field',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // domain.
  $data['backdrop_federation_following']['domain'] = array(
    'title' => t('Domain'),
    'help' => t("The instance domain of the followed account (e.g. mastodon.social)."),
    'field' => array(
      'handler' => 'views_handler_field',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // display_name.
  $data['backdrop_federation_following']['display_name'] = array(
    'title' => t('Display name'),
    'help' => t("The account's display name."),
    'field' => array(
      'handler' => 'views_handler_field',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // avatar_url.
  $data['backdrop_federation_following']['avatar_url'] = array(
    'title' => t('Avatar URL'),
    'help' => t("URL of the account's avatar image. Use \"Rewrite results\" in the field settings to render it as an &lt;img&gt; tag."),
    'field' => array(
      'handler' => 'views_handler_field_url',
      'click sortable' => FALSE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_string',
    ),
  );

  // status — pending, accepted, rejected, paused.
  $data['backdrop_federation_following']['status'] = array(
    'title' => t('Follow status'),
    'help' => t('The follow relationship status: pending, accepted, rejected, or paused.'),
    'field' => array(
      'handler' => 'views_handler_field',
      'click sortable' => TRUE,
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_in_operator',
      'options callback' => '_backdrop_federation_follow_views_follow_statuses',
    ),
    'sort' => array(
      'handler' => 'views_handler_sort',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_string',
    ),
  );

  // created — when the Follow request was sent.
  $data['backdrop_federation_following']['created'] = array(
    'title' => t('Followed date'),
    'help' => t('When this site sent the Follow request to this account.'),
    'field' => array(
      'handler' => 'views_handler_field_date',
      'click sortable' => TRUE,
    ),
    'sort' => array(
      'handler' => 'views_handler_sort_date',
    ),
    'filter' => array(
      'handler' => 'views_handler_filter_date',
    ),
    'argument' => array(
      'handler' => 'views_handler_argument_date',
    ),
  );

  return $data;
}

/**
 * Options callback: activity_type filter values.
 */
function _backdrop_federation_follow_views_activity_types() {
  return array(
    'Create' => t('Original post (Create)'),
    'Announce' => t('Boost (Announce)'),
  );
}

/**
 * Options callback: object_type filter values.
 */
function _backdrop_federation_follow_views_object_types() {
  return array(
    'Note' => t('Note'),
    'Article' => t('Article'),
  );
}

/**
 * Options callback: follow status filter values.
 */
function _backdrop_federation_follow_views_follow_statuses() {
  return array(
    'pending' => t('Pending'),
    'accepted' => t('Accepted'),
    'rejected' => t('Rejected'),
    'paused' => t('Paused'),
  );
}
