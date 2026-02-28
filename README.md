# Backdrop Federation

Enables ActivityPub federation for Backdrop CMS, connecting your site to the Fediverse. Fediverse users can follow the site and receive posts in their feeds. Replies, likes, and boosts from the Fediverse are reflected back on the site.

The core module implements an **outbound publisher** model: the site acts as an ActivityPub actor that broadcasts content to followers. An optional sub-module, **Backdrop Federation Follow**, extends this to two-way federation by allowing the site to follow remote accounts and receive their posts in a local feed. A "Fediverse Feed" block is available to display the incoming feed. 

---

## Installation

Install this module using the official Backdrop CMS instructions. Bugs and feature requests should be reported in the Issue Queue.

---

## Included Sub-modules

| Sub-module | Purpose |
|---|---|
| **Backdrop Federation Follow** | Follow remote Fediverse accounts and display their posts in a local feed |

Sub-modules are located inside the `backdrop_federation/` directory and can be enabled independently at `admin/modules`.

---

## Features

- **Site actor** — the site presents itself as an ActivityPub `Application` actor discoverable by `@username@domain` handle
- **WebFinger** — resolves `acct:name@domain` handles so remote servers can discover the actor
- **NodeInfo** — advertises software name, version, and usage statistics to the Fediverse
- **Content federation** — selected content types are automatically broadcast when nodes are published, updated, or unpublished
- **Immediate delivery** — posts are delivered to follower inboxes synchronously on node save, with cron as a fallback
- **Per-type field mapping** — choose which text field is used as the post body for each content type
- **Content negotiation** — node URLs respond with ActivityPub JSON when requested with an appropriate `Accept` header, allowing remote servers to fetch and verify notes by URL
- **Follower management** — accepts `Follow` activities, stores followers, and sends signed `Accept` responses
- **Shared inbox support** — uses a follower's shared inbox when available to reduce delivery requests
- **Re-deliver button** — re-queues all outbox entries to current followers from the admin UI
- **Outbox debug view** — browse outgoing activities and inspect their raw JSON from the admin UI
- **Replies as comments** — incoming Fediverse replies to local nodes are optionally saved as Backdrop comments
- **Comment moderation** — Fediverse replies can be held for approval before publishing
- **Likes and boosts** — incoming `Like` and `Announce` activities are counted and displayed on nodes
- **HTTP Signatures** — all outgoing requests are signed; all incoming requests are verified (draft-cavage)
- **Blocklist** — block specific actors by URI or entire domains; blocked followers are also removed
- **Fediverse handle support** — blocklist accepts `@user@domain` handles in addition to raw actor URIs
- **Rate limiting** — per-actor inbox rate limiting with configurable window and request limit
- **Subscribe page** — a form at `/fediverse/subscribe` lets visitors initiate a remote follow from their own instance
- **RSA keypair** — a 2048-bit keypair is generated on install and stored in the database

---

## Configuration

All settings are at **Administration › Configuration › Web services › Backdrop Federation** (`admin/config/services/fediverse`).

| Tab | Purpose |
|---|---|
| Settings | Enable/disable federation, actor username, display name, bio, content types, reply handling |
| Followers | List of current Fediverse followers |
| Outbox | Browse outgoing activities and inspect raw JSON (debug) |
| Blocklist | Block actors or domains; accepts raw URIs or `@user@domain` handles |
| Rate Limit | Max requests per actor per time window |

---

## How It Works

### Site Actor and Discovery

The site presents itself as a single ActivityPub `Application` actor. Remote servers discover it via two standard protocols:

- **WebFinger** (`/.well-known/webfinger`) — resolves `acct:name@domain` handles to the actor URL
- **NodeInfo** (`/.well-known/nodeinfo`, `/nodeinfo/2.1`) — advertises the software, version, and usage statistics

The actor document is served at `/fediverse/actor` and includes the site's RSA public key for HTTP signature verification. A 2048-bit RSA keypair is generated on module install and stored in the `backdrop_federation_keypair` table.

Actor settings (username, display name, bio) are configurable at `admin/config/services/fediverse`.

---

### Content Federation (Outbox)

Selected content types are federated automatically when nodes are published, updated, or unpublished.

- `hook_node_insert` and `hook_node_update` queue a `Create`, `Update`, or `Delete` activity
- Activities are stored in `backdrop_federation_outbox` and delivered immediately on node save
- Per-content-type field mapping lets you choose which text field is used as the post body
- The outbox is publicly browsable at `/fediverse/outbox` as an `OrderedCollection`

---

### Content Negotiation for Node URLs

Remote servers often fetch a note's `id` URL to verify it before processing an activity. When a request arrives at a node URL (e.g. `/node/5`) with an `Accept: application/activity+json` header, the module serves the ActivityPub JSON representation of the node directly instead of the normal HTML page. This is handled in `hook_init` and only applies to published nodes of federated content types.

---

### Followers

Remote users follow the site by sending a `Follow` activity to `/fediverse/inbox`. The module:

1. Fetches the actor document to retrieve the follower's inbox and shared inbox URIs
2. Stores the follower in `backdrop_federation_followers`
3. Sends an `Accept` activity back to the follower's inbox, signed with the site's private key

`Undo Follow` activities remove the follower from the table. A list of followers is visible at `admin/config/services/fediverse/followers`.

---

### Delivery

Posts are delivered immediately after node save by draining the delivery queue synchronously (up to a 30-second time limit). Cron processes any remaining items as a fallback.

- For each new activity, one queue item is created per unique follower inbox (preferring shared inboxes where available)
- Items are stored in Backdrop's built-in queue system (`backdrop_federation_activity_delivery`)
- A cron queue worker also processes deliveries, signing each HTTP POST with the site's private key
- The admin settings page includes a **Re-deliver** button that re-queues all outbox entries to current followers — useful when posts were published before any followers existed

---

### Inbox: Replies as Comments

When the `accept_replies` setting is enabled, incoming `Create` activities containing a `Note` or `Article` in reply to a local node URL are saved as Backdrop comments.

- The `inReplyTo` URL is validated against the local hostname and resolved to a node ID
- Duplicate detection prevents the same ActivityPub object from creating multiple comments (`backdrop_federation_received` table, keyed on object URI)
- The actor is fetched to populate the comment author name and homepage
- Comment body is passed through `filter_xss` and appended with a "Replied via the fediverse" attribution note
- A moderation setting (`comment_moderation`) holds replies for approval before publishing
- `Update` activities edit the matching comment; `Delete` activities remove it

---

### Inbox: Likes and Boosts

Incoming `Like` and `Announce` (boost) activities from Fediverse users are recorded and displayed as counts on nodes.

- Reactions are stored in `backdrop_federation_reactions` with a unique constraint on `(nid, actor_uri, type)` to prevent duplicates
- `Undo Like` and `Undo Announce` activities remove the corresponding row
- `hook_node_view` queries the counts and renders them as `<p class="backdrop-federation-reactions">N likes · M boosts</p>` on both full and teaser view modes

---

### HTTP Signatures

All outgoing requests are signed and all incoming requests are verified using HTTP Signatures (draft-cavage-http-signatures).

**Outgoing signing** (`backdrop_federation_sign_request`):
- Signs `(request-target)`, `host`, `date`, and `digest` headers
- Body digest is SHA-256, base64-encoded
- Signature uses RSA-SHA256 with the site's private key

**Incoming verification** (`backdrop_federation_verify_signature`):
- Parses the `Signature` header and extracts `keyId`, `headers`, and `signature`
- Rejects requests whose `Date` header is outside a ±5 minute window (replay protection)
- Fetches the actor document from the `keyId` URL to retrieve the remote public key
- Rebuilds the signing string from the request headers and verifies with `openssl_verify`

---

### Blocklist

Administrators can block specific actors or entire domains at `admin/config/services/fediverse/blocklist`.

- Entries are stored in `backdrop_federation_blocklist` with a `type` of `actor` or `domain`
- The check runs in the inbox endpoint after signature verification and before activity dispatch
- Actor blocks match on the full URI; domain blocks match on the hostname extracted from the actor URI via `parse_url`
- Blocked requests receive a `403 Forbidden` response
- Adding a block also removes any existing followers matching the blocked actor or domain
- Fediverse handles (`@user@domain`) are resolved via WebFinger to their actor URI automatically

---

### Rate Limiting

Inbox requests are rate-limited per actor URI to limit abuse.

- Request counts and window start times are tracked in `backdrop_federation_rate_limit`
- On each inbox request, the actor's count for the current window is checked and incremented
- If the count exceeds the configured limit, the request receives `429 Too Many Requests` with a `Retry-After: 60` header
- Window size and request limit are configurable at `admin/config/services/fediverse/rate-limit`
- Defaults: 20 requests per 60-second window
- `hook_cron` purges rows older than 10× the window to keep the table small

---

## Sub-module: Backdrop Federation Follow

The **Backdrop Federation Follow** sub-module adds two-way federation: the site can follow remote Fediverse accounts and receive their posts into a local feed.

### Features

- Follow any Fediverse account by entering a `@user@domain` handle in the admin UI
- Resolves handles via WebFinger and sends a signed `Follow` activity automatically
- Tracks follow status (Pending, Accepted, Rejected) as remote servers respond
- Stores incoming posts from followed accounts in a local feed
- Feed posts are updated or removed when the remote account edits or deletes them
- Unfollow sends a signed `Undo Follow` activity and removes the account's posts from the local feed
- **Pause / Resume** — pause a followed account to temporarily stop receiving their posts without losing the record; resume re-sends a `Follow` and resumes delivery
- **Boosts** — optionally include boosted posts in the local feed (configurable globally)
- **Feed block** — a "Fediverse Following Feed" block for placement in Backdrop layouts

### Configuration

Two additional tabs appear at `admin/config/services/fediverse` when the sub-module is enabled:

| Tab | Purpose |
|---|---|
| Following | Manage followed accounts; follow, pause, resume, or unfollow |
| Feed | Browse posts received from followed accounts (paginated) |

Feed settings (retention period, boost inclusion) are on the Following tab.

### Fediverse Following Feed Block

A **Fediverse Following Feed** block is available for placement in any Backdrop layout region. Each block instance has its own settings:

| Setting | Description |
|---|---|
| Number of posts | How many recent posts to display (5, 10, 15, 20, 25, or 50) |
| Show avatars | Display the poster's avatar next to each post |
| Truncate content | Optionally truncate post text (no limit, 140, 280, or 500 characters) |
| Max image width / height | Cap the size of inline images (100–400 px) |

- Content is sanitized with `filter_xss` at ingest time and output directly in the block
- Hashtag links are converted to plain text; regular hyperlinks are preserved
- Boosts show the booster's name and avatar alongside the original post
- A "View all posts" link at the bottom of the block links to the admin feed page

### How It Works

1. Admin enters a handle at the **Following** tab
2. The module resolves the handle via WebFinger to get the actor URI and inbox
3. A signed `Follow` activity is POSTed to the remote inbox
4. The remote server sends `Accept` or `Reject` back to `/fediverse/inbox`
5. On `Accept`, the follow status is updated and the remote server begins delivering posts
6. Incoming `Create` activities from followed accounts are stored in `backdrop_federation_feed`
7. `Update` and `Delete` activities from followed accounts update or remove the stored posts
8. `Announce` (boost) activities optionally add the boosted post to the feed; `Undo Announce` removes it

### Developer Hook

The parent module invokes `hook_backdrop_federation_inbox_activity($activity)` for every verified incoming activity. The Follow sub-module uses this to handle `Accept`, `Reject`, `Create`, `Update`, and `Delete` activities from followed accounts without modifying the parent module's inbox handler. Other modules can implement this hook to react to any incoming ActivityPub activity.

---

## Database Tables

### Core module

| Table | Purpose |
|---|---|
| `backdrop_federation_keypair` | RSA keypair for signing |
| `backdrop_federation_followers` | Remote followers and their inbox URIs |
| `backdrop_federation_outbox` | Outgoing activities |
| `backdrop_federation_received` | Incoming objects mapped to comment IDs |
| `backdrop_federation_reactions` | Like and Announce counts per node |
| `backdrop_federation_blocklist` | Blocked actors and domains |
| `backdrop_federation_rate_limit` | Per-actor request counts for rate limiting |

### Backdrop Federation Follow sub-module

| Table | Purpose |
|---|---|
| `backdrop_federation_following` | Remote accounts this site follows, with status and inbox URIs |
| `backdrop_federation_feed` | Posts received from followed accounts |

---

## ActivityPub Endpoints

All endpoints are publicly accessible. The URL paths use `/fediverse/` regardless of module name for compatibility with remote servers that have stored these URLs.

| Path | Purpose |
|---|---|
| `/.well-known/webfinger` | Actor discovery |
| `/.well-known/nodeinfo` | NodeInfo discovery |
| `/nodeinfo/2.1` | NodeInfo metadata |
| `/fediverse/actor` | Actor document |
| `/fediverse/inbox` | Incoming activities |
| `/fediverse/outbox` | Published activities |
| `/fediverse/followers` | Followers collection |
| `/fediverse/subscribe` | Remote follow form |

---

## Not Yet Implemented

The following are standard or commonly expected ActivityPub features not currently present in this module.

**Missing endpoints / collections**
- **`/fediverse/following`** — An empty collection endpoint should exist on the actor even when the Follow sub-module is not enabled; many servers expect it to be present
- **`/fediverse/liked`** — Collection of objects the actor has liked; less critical but part of the spec

**Missing actor properties**
- **`endpoints.sharedInbox`** — The actor document does not advertise its own shared inbox URI. The module already uses shared inboxes when delivering to followers, but does not expose one for incoming traffic
- **`featured`** — A collection of pinned posts, used by Mastodon to show highlighted content on the profile

**Missing content features**
- **Hashtags** — Posts should include a `tag` array with `Hashtag` objects so they appear in remote tag feeds (e.g. `#backdrop` on Mastodon)
- **Mentions** — `@user@instance` references in post content should be expressed as `tag` objects of type `Mention`
- **Content warnings** — The `summary` field on Note/Article objects is populated from the node teaser but is not exposed as a configurable content warning field

**Missing Follow sub-module features**
- **Public feed page** — the feed is currently admin-only; a configurable public-facing page would allow site visitors to see followed accounts' posts

---

## AI Statement

AI tools were used in the creation of this module with human supervision and testing.
Please use with caution and be sure to report any errors or suspicious code.

## Current Maintainer

Tim Erickson (@stpaultim)

## License

This project is GPL v2 software. See the LICENSE.txt file in this directory for complete text.
