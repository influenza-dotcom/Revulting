/* Vulture mod (IMPLEMENTATION_LOG s113): Dota-style hero barks.
 * vulture_bark(VB_x) - the hero says a short line for what they're doing:
 * shown over their head, spoken with the voices option.  Throttled inside
 * (cooldowns + chance per kind), so callers just call it. */
#ifndef VULTURE_BARK_H
#define VULTURE_BARK_H

#define VB_MOVE     0
#define VB_ATTACK   1
#define VB_KILL     2
#define VB_PICKUP   3
#define VB_DESCEND  4
#define VB_ASCEND   5
#define VB_CAST     6
#define VB_NOMANA   7
#define VB_DOOR     8
#define VB_LOCKED   9
#define VB_EAT     10
#define VB_DRINK   11
#define VB_READ    12
#define VB_PRAY    13
#define VB_LEVELUP 14
#define VB_LOWHP   15
#define VB_HUNGRY  16
#define VB_WEAR    17
#define VB_WIELD   18
#define VB_THROW   19
#define VB_ZAP     20
#define VB_COUNT   21

#ifdef __cplusplus
extern "C" {
#endif
void vulture_bark(int kind);
#ifdef __cplusplus
}
#endif

#endif /* VULTURE_BARK_H */
