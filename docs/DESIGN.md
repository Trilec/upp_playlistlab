# PlaylistLab design

## Product boundary

PlaylistLab is an ordered-playlist workbench, not a Spotify Desktop automation layer. Spotify is one provider. The local `PlaylistDocument` remains the authored state so imported lists can be edited, saved and tested without a network connection.

## Core workflow

`text / CSV / Spotify -> local document -> resolve -> edit/order -> preview plan -> publish`

A supplied Spotify URI is treated as exact identity. Otherwise a requested title/artist is resolved against provider candidates and remains visibly `Review` when confidence is not high enough for automatic selection.

## Ordering modes

- **Reference slots**: keep unrelated target rows in their current slots and reorder only rows represented by the reference list.
- **Reference first**: place matched reference rows first in reference order, then append all unrelated target rows in their existing order.

Both modes build a deterministic desired URI vector before any remote write. Spotify mutations are derived from that preview rather than being mixed into the ordering algorithm.

## Safety

- no implicit deletion of target items in the initial release;
- preview before mutation;
- snapshot-aware Spotify writes;
- exact Spotify URI identity after resolution;
- duplicate URI occurrences are preserved;
- network/provider code is kept out of deterministic planner tests.

## UI direction

The GUI follows the modern `upp_Ui` demo shell: compact title card/header actions, a large primary work surface, and a narrow contextual right rail. The centre is a multi-column ordered list with internal drag/drop; PropertyEditor is intentionally not required for the primary workflow.
