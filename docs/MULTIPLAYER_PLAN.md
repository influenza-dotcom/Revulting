# Multiplayer — groundwork plan and the decisions it needs

## The hard fact

NetHack is built around exactly one hero: `u` (the hero), `gi.invent`, vision,
the turn loop and most of the 300+ core files assume a single global player.
A true second hero in the same dungeon means touching most of the game. So the
plan grows in stages, each one playable on its own.

## Stage 1 — Co-pilot companion (recommended start)

A friend connects to your game and **controls your pet** (or any ally you
choose).

- **Server-authoritative**: the host's game is the only real game. The guest
  sends commands ("move north-east", "attack that", "follow", "stay"); the host
  applies them on the companion's own monster turns (in `dog_move`), falling
  back to the normal pet AI when the guest sends nothing.
- The guest sees the host's map: the host streams what the companion can see
  (glyph updates, the same data Vulture already draws), plus a small status
  panel for the companion.
- No changes to how NetHack treats the hero; the companion is still a monster,
  so saving, dying and bones all keep working.
- Network: a single TCP connection, host listens, guest connects by address or
  a short code; messages are small and turn-based.

## Stage 2 — Second hero ("true co-op")

Make the hero an array: two `you` structs, two inventories, per-hero vision and
messages, and a turn loop that gives each hero its moves. Very large; only worth
it if stage 1 proves the fun.

## Decisions only you can make

1. **Who waits for whom in turn-based combat?**
   - (a) Host-paced: turns advance when the host acts; the guest's companion
     acts on its own monster turns with whatever command it has queued
     (recommended for stage 1 — nothing ever blocks on the guest).
   - (b) Both must act before a turn passes (fair, but one slow player stalls
     both).
   - (c) A per-turn timer (e.g. 2 s), then the AI acts for whoever hasn't.
2. **Time modes in multiplayer**: keep real-time-until-combat, or force
   turn-based? (Recommend: real-time-until-combat, with combat using choice 1.)
3. **Pause**: can either player pause, or only the host?
4. **Death**: when the host dies the run ends for both (stage 1 follows
   naturally); is a dead companion revivable?
5. **Connection**: direct IP / port forwarding, or a relay service (easier for
   players, costs a server)?

## Suggested first build (stage 1, smallest useful slice)

1. A "Host co-op" / "Join co-op" pair of main-menu entries.
2. Host: listen on a port; guest: connect; exchange a hello and the map.
3. Guest input: click a square to send the companion there; Tab to attack the
   nearest enemy; keys for follow / stay.
4. Host: queue the guest's command; `dog_move` uses it on the pet's turn.
5. Stream the companion's view back to the guest each turn.
