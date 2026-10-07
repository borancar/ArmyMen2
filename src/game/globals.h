/* globals.h -- the original's globals by name and type.
 *
 * Game code names a global (`g_pathNodes[t].g`) and never its address. What
 * the name binds to is the build's business, and this header is the one
 * place that knows it:
 *
 *   relink (AM2_RELINK)       the typed am2_os_* extern relinkos.py declares;
 *                              relink.py resolves the symbol to its VA.
 *   standalone, native,       the typed member of am2_origstate, wherever
 *   vc6-link (AM2_STANDALONE) the link put it.
 *   injected hook DLL         the running original's own storage, which is
 *                              where orig.h says it is.
 *
 * Types live in globaltypes.h, shared with origstate.h and the relink header
 * so a global is declared with one type everywhere.
 *
 * Most names below are not typed declarations yet: they bind, in every build,
 * to the expression the code used to spell at each use -- a dereference of
 * the address orig.h gives (the standalone and relink headers redirect that
 * address to the build's own storage). A name that the code reads at another
 * width is read as `*(T *)&g_x` at that use, and the comment is the
 * original's address. Moving one to a real typed declaration, as g_pathNodes
 * is above, changes only its line here. Globals of a Win32 or COM type are
 * in win32/globals.h. */
#ifndef AM2_GLOBALS_H
#define AM2_GLOBALS_H


#if defined(AM2_RELINK)
#define g_pathNodes am2_os_path_nodes
#elif defined(AM2_STANDALONE)
#define g_pathNodes am2_origstate.path_nodes
#else
#include "globaltypes.h"
#define g_pathNodes (*(AM2_PathNode (*)[0x10000])(uintptr_t)ADDR_PATH_NODES)
#endif

#define g_aaiKey980000           (*(int32_t *)(uintptr_t)ADDR_AAI_KEY_980000)  /* 0x00516158 */
#define g_aaiKey980080           ((uint8_t *)(uintptr_t)ADDR_AAI_KEY_980080)  /* 0x0051615C */
#define g_aaiKey980100           ((uint8_t *)(uintptr_t)ADDR_AAI_KEY_980100)  /* 0x00516168 */
#define g_aaiRecordCap           (*(int32_t *)(uintptr_t)ADDR_AAI_RECORD_CAP)  /* 0x00516144 */
#define g_aaiRecords             (*(struct AM2_AaiRec ***)(uintptr_t)ADDR_AAI_RECORDS)  /* 0x0051614C */
#define g_actionCrouch           ((int32_t *)(uintptr_t)ADDR_ACTION_CROUCH)  /* 0x00475090 */
#define g_actionKey1             ((int32_t *)(uintptr_t)ADDR_ACTION_KEY1)  /* 0x004750CC */
#define g_actionKey4             ((int32_t *)(uintptr_t)ADDR_ACTION_KEY4)  /* 0x004750D8 */
#define g_actionKey5             ((int32_t *)(uintptr_t)ADDR_ACTION_KEY5)  /* 0x004750E4 */
#define g_actionProne            ((int32_t *)(uintptr_t)ADDR_ACTION_PRONE)  /* 0x004750A8 */
#define g_activePalette          (*(uint32_t **)(uintptr_t)ADDR_ACTIVE_PALETTE)  /* 0x00477A58 */
#define g_aimDamage              (*(int32_t *)(uintptr_t)ADDR_AIM_DAMAGE)  /* 0x00662838 */
#define g_aimDeadlineA           ((uint8_t *)(uintptr_t)ADDR_AIM_DEADLINE_A)  /* 0x004FC910 */
#define g_aimDeadlineB           ((int32_t *)(uintptr_t)ADDR_AIM_DEADLINE_B)  /* 0x004FC984 */
#define g_aimDisplaceMap         ((int16_t *)(uintptr_t)ADDR_AIM_DISPLACE_MAP)  /* 0x00478CDC */
#define g_aimFrameB              ((int32_t *)(uintptr_t)ADDR_AIM_FRAME_B)  /* 0x004FC964 */
#define g_aimLifeHalfA           (*(int32_t *)(uintptr_t)ADDR_AIM_LIFE_HALF_A)  /* 0x00662820 */
#define g_aimLifeHalfB           (*(int32_t *)(uintptr_t)ADDR_AIM_LIFE_HALF_B)  /* 0x00662854 */
#define g_aimLiveA               ((int32_t *)(uintptr_t)ADDR_AIM_LIVE_A)  /* 0x004FC8E0 */
#define g_aimLiveB               ((int32_t *)(uintptr_t)ADDR_AIM_LIVE_B)  /* 0x004FC944 */
#define g_aiMoveState            ((int32_t *)(uintptr_t)ADDR_AI_MOVE_STATE)  /* 0x004750B4 */
#define g_aiMoveStateAlt         ((int32_t *)(uintptr_t)ADDR_AI_MOVE_STATE_ALT)  /* 0x004750C0 */
#define g_aimPointA              ((uint8_t *)(uintptr_t)ADDR_AIM_POINT_A)  /* 0x004FC8F0 */
#define g_aimPointB              ((uint8_t *)(uintptr_t)ADDR_AIM_POINT_B)  /* 0x004FC954 */
#define g_aimSpawnArg            (*(int32_t *)(uintptr_t)ADDR_AIM_SPAWN_ARG)  /* 0x0066286C */
#define g_aimSpritesA            ((AM2_Sprite **)(uintptr_t)ADDR_AIM_SPRITES_A)  /* 0x004FC8C8 */
#define g_aimSpritesB            ((AM2_Sprite **)(uintptr_t)ADDR_AIM_SPRITES_B)  /* 0x004FC920 */
#define g_aimStampA              ((int32_t *)(uintptr_t)ADDR_AIM_STAMP_A)  /* 0x004FC900 */
#define g_aimStampB              ((int32_t *)(uintptr_t)ADDR_AIM_STAMP_B)  /* 0x004FC974 */
#define g_aimX                   (*(uint32_t *)(uintptr_t)ADDR_AIM_X)  /* 0x00512568 */
#define g_aimSpot                (*(AM2_FireSpot *)(uintptr_t)ADDR_AIM_X)  /* 0x00512568, x/y and the ground height */
#define g_aimY                   (*(int16_t *)(uintptr_t)ADDR_AIM_Y)  /* 0x0051256A */
#define g_aimZ                   (*(uint16_t *)(uintptr_t)ADDR_AIM_Z)  /* 0x0051256C */
#define g_airCycleMs             (*(int32_t *)(uintptr_t)ADDR_AIR_CYCLE_MS)  /* 0x00473F38 */
#define g_airDropFacings         ((uint8_t *)(uintptr_t)ADDR_AIR_DROP_FACINGS)  /* 0x00473F80 */
#define g_airDropOffsets         ((int16_t *)(uintptr_t)ADDR_AIR_DROP_OFFSETS)  /* 0x00473F74 */
#define g_airFrameHotspots       ((int16_t *)(uintptr_t)ADDR_AIR_FRAME_HOTSPOTS)  /* 0x00473FB0 */
#define g_airGaugeMs             (*(int32_t *)(uintptr_t)ADDR_AIR_GAUGE_MS)  /* 0x00473F30 */
#define g_airGaugeSlope          (*(double *)(uintptr_t)ADDR_AIR_GAUGE_SLOPE)  /* 0x00473F28 */
#define g_airGaugeX0             (*(int32_t *)(uintptr_t)ADDR_AIR_GAUGE_X0)  /* 0x00473F20 */
#define g_airGaugeY0             (*(int32_t *)(uintptr_t)ADDR_AIR_GAUGE_Y0)  /* 0x00473F24 */
#define g_airLeg1Dx              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG1_DX)  /* 0x004F96B0 */
#define g_airLeg1Dy              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG1_DY)  /* 0x004F93C8 */
#define g_airLeg1Ms              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG1_MS)  /* 0x00473F40 */
#define g_airLeg1Slope           (*(double *)(uintptr_t)ADDR_AIR_LEG1_SLOPE)  /* 0x00473F50 */
#define g_airLeg1X0              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG1_X0)  /* 0x004F96AC */
#define g_airLeg2Divisor         (*(float *)(uintptr_t)ADDR_AIR_LEG2_DIVISOR)  /* 0x004F93CC */
#define g_airLeg2Dy              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG2_DY)  /* 0x004F96A4 */
#define g_airLeg2Ms              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG2_MS)  /* 0x00473F44 */
#define g_airLeg2MsSpan          (*(int32_t *)(uintptr_t)ADDR_AIR_LEG2_MS_SPAN)  /* 0x004F93D0 */
#define g_airLeg3Dx              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG3_DX)  /* 0x004F93D4 */
#define g_airLeg3Dy              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG3_DY)  /* 0x004F96A8 */
#define g_airLeg3Slope           (*(double *)(uintptr_t)ADDR_AIR_LEG3_SLOPE)  /* 0x00473F58 */
#define g_airLeg3X1              (*(int32_t *)(uintptr_t)ADDR_AIR_LEG3_X1)  /* 0x004F96B4 */
#define g_airPassMs              (*(int32_t *)(uintptr_t)ADDR_AIR_PASS_MS)  /* 0x00473F34 */
#define g_airPathApexX           (*(int32_t *)(uintptr_t)ADDR_AIR_PATH_APEX_X)  /* 0x00473F64 */
#define g_airPathAwayX           (*(int16_t *)(uintptr_t)ADDR_AIR_PATH_AWAY_X)  /* 0x00473F70 */
#define g_airPathHalfY           (*(int32_t *)(uintptr_t)ADDR_AIR_PATH_HALF_Y)  /* 0x00473F68 */
#define g_airPathInY             (*(int32_t *)(uintptr_t)ADDR_AIR_PATH_IN_Y)  /* 0x00473F48 */
#define g_airPathMidY            (*(int32_t *)(uintptr_t)ADDR_AIR_PATH_MID_Y)  /* 0x00473F60 */
#define g_airPathOutY            (*(int32_t *)(uintptr_t)ADDR_AIR_PATH_OUT_Y)  /* 0x00473F4C */
#define g_airPathTurnX           (*(int16_t *)(uintptr_t)ADDR_AIR_PATH_TURN_X)  /* 0x00473F6C */
#define g_airPathTurnYIn         (*(int16_t *)(uintptr_t)ADDR_AIR_PATH_TURN_Y_IN)  /* 0x00473F6E */
#define g_airPathTurnYOut        (*(int16_t *)(uintptr_t)ADDR_AIR_PATH_TURN_Y_OUT)  /* 0x00473F72 */
#define g_airRunMs               (*(int32_t *)(uintptr_t)ADDR_AIR_RUN_MS)  /* 0x00473F3C */
#define g_airSaveBlock           ((struct AM2_AirBlock *)(uintptr_t)ADDR_AIR_SAVE_BLOCK)  /* 0x004F945C */
#define g_airSprites2            (*(AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_2)  /* 0x004F93D8 */
#define g_airSprites3            ((AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_3)  /* 0x004F93DC */
#define g_airSprites6            ((AM2_Sprite **)(uintptr_t)ADDR_AIR_SPRITES_6)  /* 0x004F942C */
#define g_airSpritesEdge         (*(int32_t *)(uintptr_t)ADDR_AIR_SPRITES_EDGE)  /* 0x004F9458 */
#define g_airStrikeKinds         ((int32_t *)(uintptr_t)ADDR_AIR_STRIKE_KINDS)  /* 0x00473FDC */
#define g_airStrikeSlope         (*(double *)(uintptr_t)ADDR_AIR_STRIKE_SLOPE)  /* 0x00473F28 */
#define g_allyMatrix             ((int32_t *)(uintptr_t)ADDR_ALLY_MATRIX)  /* 0x00511E60 */
#define g_animFrame              (*(int32_t *)(uintptr_t)ADDR_MENU_ANIM_FRAME)  /* 0x004FCDEC */
#define g_animNext               (*(uint32_t *)(uintptr_t)ADDR_MENU_ANIM_NEXT)  /* 0x004FCDE8 */
#define g_appActive              (*(int32_t *)(uintptr_t)ADDR_APP_ACTIVE)  /* 0x004FA02C */
#define g_appGuid                ((uint8_t *)(uintptr_t)ADDR_APP_GUID)  /* 0x0046F8A8 */
#define g_armyObjLists           ((struct AM2_PtrList **)(uintptr_t)ADDR_ARMY_OBJ_LISTS)  /* 0x004F9ECC */
#define g_armyPacket             ((uint8_t *)(uintptr_t)ADDR_ARMY_PACKET)  /* 0x004FAA68 */
#define g_armyPacketB            ((uint8_t *)(uintptr_t)ADDR_ARMY_PACKET_B)  /* 0x004FB270 */
#define g_armyPacketSeq          (*(int32_t *)(uintptr_t)ADDR_ARMY_PACKET_SEQ)  /* 0x004FAA70 */
#define g_armyPalBase            ((uint8_t *)(uintptr_t)ADDR_ARMY_PAL_BASE)  /* 0x00474174 */
#define g_armyPoints             ((int32_t *)(uintptr_t)ADDR_ARMY_POINTS)  /* 0x00515FE0 */
#define g_armyRampTables         ((uint8_t *)(uintptr_t)ADDR_ARMY_RAMP_TABLES)  /* 0x004F96CC */
#define g_armyTable              (*(void **)(uintptr_t)ADDR_ARMY_TABLE)  /* 0x004751B0 */
#define g_atanCos                ((int8_t *)(uintptr_t)ADDR_TRIG_ATAN_COS)  /* 0x00515D84 */
#define g_atanSin                ((int8_t *)(uintptr_t)ADDR_TRIG_ATAN_SIN)  /* 0x00515580 */
#define g_atEnd                  (*(int32_t *)(uintptr_t)ADDR_AUDIO_AT_END)  /* 0x004FA460 */
#define g_attemptCount           (*(int32_t *)(uintptr_t)ADDR_ATTEMPT_COUNT)  /* 0x00512330 */
#define g_audioEnabled           (*(int32_t *)(uintptr_t)ADDR_AUDIO_ENABLED)  /* 0x004FA468 */
#define g_backgroundColour       (*(uint8_t *)(uintptr_t)ADDR_BACKGROUND_COLOUR)  /* 0x00502AD9 */
#define g_bandwidthLastSeq       (*(uint32_t *)(uintptr_t)ADDR_BANDWIDTH_LAST_SEQ)  /* 0x004FC8C0 */
#define g_bitFromN               ((void *)(uintptr_t)ADDR_BIT_FROM_N)  /* 0x00487814 */
#define g_bitmapAreaH            (*(int32_t *)(uintptr_t)ADDR_BITMAP_AREA_H)  /* 0x0048531C */
#define g_bitmapAreaW            (*(int32_t *)(uintptr_t)ADDR_BITMAP_AREA_W)  /* 0x00485318 */
#define g_blitRect               ((AM2_Rect *)(uintptr_t)ADDR_BLIT_RECT)  /* 0x00485320 */
#define g_bufferSize             (*(uint32_t *)(uintptr_t)ADDR_AUDIO_BUFFER_SIZE)  /* 0x004FA444 */
#define g_buildMenu              ((uint8_t *)(uintptr_t)ADDR_BUILD_MENU)  /* 0x004762D0 */
#define g_buildMenuEnd           ((uint8_t *)(uintptr_t)ADDR_BUILD_MENU_END)  /* 0x004766C0 */
#define g_buildMenuRects         ((uint8_t *)(uintptr_t)ADDR_BUILD_MENU_RECTS)  /* 0x004762C0 */
#define g_cameraX                (*(int32_t *)(uintptr_t)ADDR_CAMERA_X)  /* 0x00514EA8 */
#define g_cameraY                (*(int32_t *)(uintptr_t)ADDR_CAMERA_Y)  /* 0x00514EAC */
#define g_cdFoundFlag            (*(int32_t *)(uintptr_t)ADDR_CD_FOUND_FLAG)  /* 0x00512594 */
#define g_cdPath                 ((char *)(uintptr_t)ADDR_CD_PATH)  /* 0x00512464 */
#define g_cdPresent              (*(int32_t *)(uintptr_t)ADDR_CD_PRESENT)  /* 0x00512588 */
#define g_cdRequiredCaption      ((char *)(uintptr_t)ADDR_CD_REQUIRED_CAPTION)  /* 0x004755B4 */
#define g_cdRequiredText         ((char *)(uintptr_t)ADDR_CD_REQUIRED_TEXT)  /* 0x00475578 */
#define g_cellWeights            (*(int8_t **)(uintptr_t)ADDR_CELL_WEIGHTS)  /* 0x00514EC0 */
#define g_changeLimitFloor       (*(uint32_t *)(uintptr_t)ADDR_CHANGE_LIMIT_FLOOR)  /* 0x004FC8BC */
#define g_charHandler            (*(void **)(uintptr_t)ADDR_CHAR_HANDLER)  /* 0x005125B8 */
#define g_chatColourTable        ((uint8_t *)(uintptr_t)ADDR_CHAT_COLOUR_TABLE)  /* 0x004F9AD0 */
#define g_cheatEnabled           (*(int32_t *)(uintptr_t)ADDR_CHEAT_ENABLED)  /* 0x004FCF94 */
#define g_cheatLevelSelect       (*(int32_t *)(uintptr_t)ADDR_CHEAT_LEVEL_SELECT)  /* 0x004FCF98 */
#define g_cheatWords             ((char **)(uintptr_t)ADDR_CHEAT_WORDS)  /* 0x00476704 */
#define g_clockBaseMs            (*(int32_t *)(uintptr_t)ADDR_CLOCK_BASE_MS)  /* 0x00511E00 */
#define g_colourBelowBg          (*(uint8_t *)(uintptr_t)ADDR_COLOUR_BELOW_BG)  /* 0x00502AD8 */
#define g_colourBlack            (*(uint8_t *)(uintptr_t)ADDR_COLOUR_BLACK)  /* 0x004FE094 */
#define g_colourBlue             (*(uint8_t *)(uintptr_t)ADDR_COLOUR_BLUE)  /* 0x004FDF7C */
#define g_colourCream            (*(uint8_t *)(uintptr_t)ADDR_COLOUR_CREAM)  /* 0x004FDF75 */
#define g_colourDarkBlue         ((uint8_t *)(uintptr_t)ADDR_COLOUR_DARK_BLUE)  /* 0x004FD760 */
#define g_colourDarkGreen        (*(uint8_t *)(uintptr_t)ADDR_COLOUR_DARK_GREEN)  /* 0x004FDF74 */
#define g_colourDarkGrey         (*(uint8_t *)(uintptr_t)ADDR_COLOUR_DARK_GREY)  /* 0x00502CE4 */
#define g_colourLagMid           (*(uint8_t *)(uintptr_t)ADDR_COLOUR_LAG_MID)  /* 0x004FE092 */
#define g_colourLightBlue        (*(uint8_t *)(uintptr_t)ADDR_COLOUR_LIGHT_BLUE)  /* 0x004FE1AC */
#define g_colourLightGreen       (*(uint8_t *)(uintptr_t)ADDR_COLOUR_LIGHT_GREEN)  /* 0x004FE091 */
#define g_colourLightGrey        (*(uint8_t *)(uintptr_t)ADDR_COLOUR_LIGHT_GREY)  /* 0x004FE093 */
#define g_colourNoMap            (*(uint8_t *)(uintptr_t)ADDR_COLOUR_NO_MAP)  /* 0x00502CE5 */
#define g_colourOlive            (*(uint8_t *)(uintptr_t)ADDR_COLOUR_OLIVE)  /* 0x004FE1AE */
#define g_colourStale            (*(uint8_t *)(uintptr_t)ADDR_COLOUR_STALE)  /* 0x004FE090 */
#define g_colourSteelBlue        (*(uint8_t *)(uintptr_t)ADDR_COLOUR_STEEL_BLUE)  /* 0x004FE088 */
#define g_colourWhiteB           (*(uint8_t *)(uintptr_t)ADDR_COLOUR_WHITE_B)  /* 0x004FE1AD */
#define g_commEnumCount          (*(int32_t *)(uintptr_t)ADDR_COMM_ENUM_COUNT)  /* 0x004751B4 */
#define g_commGlobal             ((void *)(uintptr_t)ADDR_COMM_GLOBAL)  /* 0x004FA480 */
#define g_commObject             (*(AM2_Comm **)(uintptr_t)ADDR_COMM_OBJECT)  /* 0x004751B0 */
#define g_connectionList         (*(void **)(uintptr_t)ADDR_CONNECTION_LIST)  /* 0x004FA900 */
#define g_cpunormspeed           (*(am2_cpuspeed_fn *)(uintptr_t)ADDR_CPUNORMSPEED_FN)  /* 0x004F9FE8 */
#define g_createWatchedKind      (*(int32_t *)(uintptr_t)ADDR_CREATE_WATCHED_KIND)  /* 0x00516160 */
#define g_curKeys                (*(uint8_t **)(uintptr_t)ADDR_KEYS_NOW_PTR)  /* 0x005127C8 */
#define g_currentBitmap          (*(struct AM2_Sprite **)(uintptr_t)ADDR_CURRENT_BITMAP)  /* 0x005122C4 */
#define g_currentObjScript       (*(int32_t *)(uintptr_t)ADDR_CURRENT_OBJ_SCRIPT)  /* 0x0051618C */
#define g_cursorA                (*(int32_t *)(uintptr_t)ADDR_AUDIO_CURSOR_A)  /* 0x004FA450 */
#define g_cursorB                (*(int32_t *)(uintptr_t)ADDR_AUDIO_CURSOR_B)  /* 0x004FA44C */
#define g_cursorPoint            (*(int32_t *)(uintptr_t)ADDR_CURSOR_POINT)  /* 0x0048546C */
#define g_cursorX                (*(int32_t *)(uintptr_t)ADDR_CURSOR_X)  /* 0x00485464 */
#define g_cursorY                (*(int32_t *)(uintptr_t)ADDR_CURSOR_Y)  /* 0x00485468 */
#define g_dataChecksum           (*(int32_t *)(uintptr_t)ADDR_DATA_CHECKSUM)  /* 0x004FC8B4 */
#define g_dbl512                 ((uint8_t *)(uintptr_t)ADDR_DBL_512)  /* 0x0046F9B0 */
#define g_dblMaxPeriod           (*(double *)(uintptr_t)ADDR_DBL_MAX_PERIOD)  /* 0x0046F988 */
#define g_dblMsPerSec            (*(double *)(uintptr_t)ADDR_DBL_MS_PER_SEC)  /* 0x0046F990 */
#define g_dblOne256              ((uint8_t *)(uintptr_t)ADDR_DBL_ONE_256)  /* 0x0046F9C0 */
#define g_dblSinScale            ((uint8_t *)(uintptr_t)ADDR_DBL_SIN_SCALE)  /* 0x0046F9B8 */
#define g_dblTwoPi               ((uint8_t *)(uintptr_t)ADDR_DBL_TWO_PI)  /* 0x0046F9C8 */
#define g_dblZero                ((uint8_t *)(uintptr_t)ADDR_DBL_ZERO)  /* 0x0046F920 */
#define g_deathAnimByCode        ((int32_t *)(uintptr_t)ADDR_DEATH_ANIM_BY_CODE)  /* 0x0047518C */
#define g_debugBlastKind         (*(int32_t *)(uintptr_t)ADDR_DEBUG_BLAST_KIND)  /* 0x00476FB4 */
#define g_debugItemList          (*(int32_t *)(uintptr_t)ADDR_DEBUG_ITEMLIST)  /* 0x004FD73C */
#define g_decalSpriteCount       (*(int32_t *)(uintptr_t)ADDR_DECAL_SPRITE_COUNT)  /* 0x0048CBA4 */
#define g_decalSprites           (*(struct AM2_Sprite ***)(uintptr_t)ADDR_DECAL_SPRITES)  /* 0x0048CBA0 */
#define g_defaultOwner           (*(uint32_t *)(uintptr_t)ADDR_DEFAULT_OWNER)  /* 0x004F9FDC */
#define g_defaultPalette         (*(void **)(uintptr_t)ADDR_DEFAULT_PALETTE)  /* 0x004FE084 */
#define g_defaultPaletteBuf      ((uint8_t *)(uintptr_t)ADDR_DEFAULT_PALETTE_BUF)  /* 0x00502BDC */
#define g_defLinkCap             (*(int32_t *)(uintptr_t)ADDR_DEF_LINK_CAP)  /* 0x00516184 */
#define g_defLinkCount           (*(int32_t *)(uintptr_t)ADDR_DEF_LINK_COUNT)  /* 0x00516180 */
#define g_defLinks               (*(AM2_DefLink **)(uintptr_t)ADDR_DEF_LINKS)  /* 0x0051617C */
#define g_defMissileCap          (*(int32_t *)(uintptr_t)ADDR_DEF_MISSILE_CAP)  /* 0x00662930 */
#define g_defMissileCount        (*(int32_t *)(uintptr_t)ADDR_DEF_MISSILE_COUNT)  /* 0x0066292C */
#define g_defMissileRecs         (*(uint8_t **)(uintptr_t)ADDR_DEF_MISSILE_RECS)  /* 0x00662928 */
#define g_defNameTable           ((AM2_DefKeyword *)(uintptr_t)ADDR_DEF_NAME_TABLE)  /* 0x00476FE0 */
#define g_defObjRecCap           (*(int32_t *)(uintptr_t)ADDR_DEF_OBJ_REC_CAP)  /* 0x00516178 */
#define g_defObjRecCount         (*(int32_t *)(uintptr_t)ADDR_DEF_OBJ_REC_COUNT)  /* 0x00516174 */
#define g_defObjRecs             (*(void **)(uintptr_t)ADDR_DEF_OBJ_RECS)  /* 0x00516170 */
#define g_defTrooperCap          (*(int32_t *)(uintptr_t)ADDR_DEF_TROOPER_CAP)  /* 0x00659F54 */
#define g_defTrooperCount        (*(int32_t *)(uintptr_t)ADDR_DEF_TROOPER_COUNT)  /* 0x00659F50 */
#define g_defTrooperRecs         (*(void **)(uintptr_t)ADDR_DEF_TROOPER_RECS)  /* 0x00659F4C */
#define g_depthCount             (*(int32_t *)(uintptr_t)ADDR_DEPTH_COUNT)  /* 0x0050B1D4 */
#define g_depthCursor            (*(AM2_DepthNode **)(uintptr_t)ADDR_DEPTH_CURSOR)  /* 0x00507348 */
#define g_depthDC                (*(int32_t *)(uintptr_t)ADDR_DEPTH_FIELD_DC)  /* 0x0050B1DC */
#define g_depthHead              (*(int32_t *)(uintptr_t)ADDR_DEPTH_HEAD)  /* 0x0050B1D8 */
#define g_depthNodes             ((AM2_DepthNode *)(uintptr_t)ADDR_DEPTH_NODES)  /* 0x00507350 */
#define g_dfKeyboard             ((uint8_t *)(uintptr_t)ADDR_DF_KEYBOARD)  /* 0x0046FD68 */
#define g_dfMouse                ((uint8_t *)(uintptr_t)ADDR_DF_MOUSE)  /* 0x0046FD80 */
#define g_difficulty             (*(int32_t *)(uintptr_t)ADDR_DIFFICULTY)  /* 0x00512324 */
#define g_difficultyScale        ((float *)(uintptr_t)ADDR_DIFFICULTY_SCALE)  /* 0x00489870 */
#define g_dipropBufferSize       ((uint8_t *)(uintptr_t)ADDR_DIPROP_BUFFER_SIZE)  /* 0x004854F8 */
#define g_dirScratch             ((char *)(uintptr_t)ADDR_DIR_SCRATCH)  /* 0x004F96B8 */
#define g_dirTitlePtr            (*(char **)(uintptr_t)ADDR_DIR_TITLE_PTR)  /* 0x004852D0 */
#define g_dirtyHead              (*(uint16_t *)(uintptr_t)ADDR_DIRTY_HEAD)  /* 0x00508AD6 */
#define g_dirtyPrevHead          (*(uint16_t *)(uintptr_t)ADDR_DIRTY_PREV_HEAD)  /* 0x00508AD4 */
#define g_dirtyRects             ((AM2_DirtyRec *)(uintptr_t)ADDR_DIRTY_RECTS)  /* 0x00508AC4 */
#define g_dirtyTail              (*(uint16_t *)(uintptr_t)ADDR_DIRTY_TAIL)  /* 0x00508AC0 */
#define g_dragAnchor             (*(AM2_Point *)(uintptr_t)ADDR_DRAG_ANCHOR)  /* 0x004FCF68 */
#define g_drawTarget             (*(void **)(uintptr_t)ADDR_DRAW_TARGET)  /* 0x00507128 */
#define g_dropRing               ((int16_t *)(uintptr_t)ADDR_DROP_RING)  /* 0x00489DE8 */
#define g_dynamic                ((struct AM2_Sound **)(uintptr_t)ADDR_SOUND_DYNAMIC)  /* 0x004FA3C0 */
#define g_editCharset            (*(char **)(uintptr_t)ADDR_EDIT_CHARSET_PTR)  /* 0x00485308 */
#define g_editCharsetDefault     (*(char **)(uintptr_t)ADDR_EDIT_CHARSET_DEFAULT)  /* 0x00485304 */
#define g_endState               (*(int32_t *)(uintptr_t)ADDR_GAME_OVER_STATE)  /* 0x00515F88 */
#define g_enemyHealthShare       (*(double *)(uintptr_t)ADDR_ENEMY_HEALTH_SHARE)  /* 0x0046FD50 */
#define g_enumContext            (*(void **)(uintptr_t)ADDR_HWND)  /* 0x0051245C */
#define g_errorText              ((char *)(uintptr_t)ADDR_ERROR_TEXT)  /* 0x0050B5E0 */
#define g_errorTextDD            ((char *)(uintptr_t)ADDR_ERROR_TEXT_DD)  /* 0x0050B1E0 */
#define g_eventBlock             (*(uint32_t *)(uintptr_t)ADDR_EVENT_BLOCK)  /* 0x0050C368 */
#define g_eventFlags             (*(uint32_t *)(uintptr_t)ADDR_PAUSE_FLAGS)  /* 0x005122FC */
#define g_eventTable             ((AM2_EventEntry **)(uintptr_t)ADDR_EVENT_TABLE)  /* 0x005101F0 */
#define g_evtId15Flag            (*(int32_t *)(uintptr_t)ADDR_EVT_ID15_FLAG)  /* 0x00511E48 */
#define g_evtId15Uid             (*(int32_t *)(uintptr_t)ADDR_EVT_ID15_UID)  /* 0x00511E20 */
#define g_exitGameFlag           (*(int32_t *)(uintptr_t)ADDR_EXIT_GAME_FLAG)  /* 0x004F8778 */
#define g_explosionAnims         ((struct AM2_AnimTable *)(uintptr_t)ADDR_EXPLOSION_ANIMS)  /* 0x00510228 */
#define g_explosionArea16        (*(AM2_Rect *)(uintptr_t)ADDR_EXPLOSION_AREA_16)  /* 0x004788F0 */
#define g_explosionArea24        (*(AM2_Rect *)(uintptr_t)ADDR_EXPLOSION_AREA_24)  /* 0x00478900 */
#define g_explosionArea32        (*(AM2_Rect *)(uintptr_t)ADDR_EXPLOSION_AREA_32)  /* 0x00478910 */
#define g_explosionBox           ((const AM2_Rect *)(uintptr_t)ADDR_EXPLOSION_BOX)  /* 0x004788E0 */
#define g_explosionRowSpec       ((const struct AM2_RowSpec *)(uintptr_t)ADDR_EXPLOSION_ROW_SPEC)  /* 0x00478920 */
#define g_fastMachine            (*(int32_t *)(uintptr_t)ADDR_FAST_MACHINE)  /* 0x00512584 */
#define g_field530Frames         ((int32_t *)(uintptr_t)ADDR_FIELD_530_FRAMES)  /* 0x00487BF8 */
#define g_fixedStep              (*(int32_t *)(uintptr_t)ADDR_FIXED_STEP)  /* 0x00512598 */
#define g_flameNextMs            (*(uint32_t *)(uintptr_t)ADDR_FLAME_NEXT_MS)  /* 0x004FCFA4 */
#define g_flameOn                (*(int32_t *)(uintptr_t)ADDR_FLAME_ON)  /* 0x004FCFA0 */
#define g_flameRecord            ((void *)(uintptr_t)ADDR_FLAME_RECORD)  /* 0x004FCDF8 */
#define g_floatZero              (*(float *)(uintptr_t)ADDR_FLOAT_ZERO)  /* 0x0046F928 */
#define g_focusedEdit            (*(AM2_Widget **)(uintptr_t)ADDR_FOCUSED_EDIT)  /* 0x0065A05C */
#define g_fogOfWar               (*(int32_t *)(uintptr_t)ADDR_FOG_OF_WAR)  /* 0x00476FB0 */
#define g_fontDescs              ((AM2_FontDesc *)(uintptr_t)ADDR_FONT_DESCS)  /* 0x004897E8 */
#define g_formationSlots         ((AM2_FormationSlot *)(uintptr_t)ADDR_FORMATION_SLOTS)  /* 0x00473EA0 */
#define g_frameBuf               (*(uint8_t **)(uintptr_t)ADDR_FRAMEBUFFER)  /* 0x004FE1A8 */
#define g_frameDeltaMs           (*(int32_t *)(uintptr_t)ADDR_FRAME_DELTA_MS)  /* 0x00511E08 */
#define g_frameDeltaSec          (*(float *)(uintptr_t)ADDR_FRAME_DELTA_SEC)  /* 0x00511E10 */
#define g_frameHeadingBias       ((uint8_t *)(uintptr_t)ADDR_FRAME_HEADING_BIAS)  /* 0x004740CC */
#define g_fullRedraw             (*(int32_t *)(uintptr_t)ADDR_FULL_REDRAW)  /* 0x00512460 */
#define g_gameClockMs            (*(uint32_t *)(uintptr_t)ADDR_GAME_CLOCK_MS)  /* 0x00511E04 */
#define g_gameConstants          (*(int8_t *)(uintptr_t)ADDR_GAME_CONSTANTS)  /* 0x00487BA8 */
#define g_gameDir                ((char *)(uintptr_t)ADDR_GAME_DIR)  /* 0x0051235C */
#define g_gameOverFlags          (*(uint32_t *)(uintptr_t)ADDR_GAME_OVER_FLAGS)  /* 0x00515FD8 */
#define g_gameOverSaved          ((int32_t *)(uintptr_t)ADDR_GAME_OVER_SAVED)  /* 0x00515F8C */
#define g_gameOverSource         ((int32_t *)(uintptr_t)ADDR_GAME_OVER_SOURCE)  /* 0x00511E14 */
#define g_gameprocBlock          (*(char *)(uintptr_t)ADDR_GAMEPROC_BLOCK)  /* 0x00511A68 */
#define g_gameprocStrB           (*(char *)(uintptr_t)ADDR_GAMEPROC_STR_B)  /* 0x00511B88 */
#define g_gameSeed               (*(int32_t *)(uintptr_t)ADDR_GAME_SEED)  /* 0x00512314 */
#define g_gameSeedSent           (*(int32_t *)(uintptr_t)ADDR_GAME_SEED_SENT)  /* 0x004FC780 */
#define g_gameSetting22c         (*(int32_t *)(uintptr_t)ADDR_GAME_SETTING_22C)  /* 0x00515FDC */
#define g_gameState              (*(int32_t *)(uintptr_t)ADDR_GAME_STATE)  /* 0x00511DA4 */
#define g_gameVersion            (*(int32_t *)(uintptr_t)ADDR_GAME_VERSION)  /* 0x00475894 */
#define g_gameWinner             (*(int32_t *)(uintptr_t)ADDR_GAME_WINNER)  /* 0x00512300 */
#define g_gdiPalette             ((uint8_t *)(uintptr_t)ADDR_GDI_PALETTE)  /* 0x00477E6C */
#define g_fonts                  ((AM2_FontRec *)(uintptr_t)ADDR_GLYPH_SIZE)  /* 0x006598D0 */
#define g_gravity                (*(float *)(uintptr_t)ADDR_GRAVITY)  /* 0x004852EC */
#define g_haveDefaultCof         (*(int32_t *)(uintptr_t)ADDR_HAVE_DEFAULT_COF)  /* 0x00511DDC */
#define g_hiliteColour           (*(uint8_t *)(uintptr_t)ADDR_VIEW_RECT_COLOUR)  /* 0x004FE089 */
#define g_hitPoseByClass         ((int32_t *)(uintptr_t)ADDR_HIT_POSE_BY_CLASS)  /* 0x00475198 */
#define g_hostMaskA              (*(uint32_t *)(uintptr_t)ADDR_HOST_MASK_A)  /* 0x00516078 */
#define g_hostMaskB              (*(uint32_t *)(uintptr_t)ADDR_HOST_MASK_B)  /* 0x0051607C */
#define g_hostValue3e8           (*(int32_t *)(uintptr_t)ADDR_HOST_VALUE_3E8)  /* 0x00516090 */
#define g_hudCmdOffsets          ((int16_t *)(uintptr_t)ADDR_HUD_CMD_OFFSETS)  /* 0x004766F8 */
#define g_hudCmdSpec             ((uint8_t *)(uintptr_t)ADDR_HUD_CMD_SPEC)  /* 0x004761A8 */
#define g_hudCmdSprites          ((AM2_Sprite **)(uintptr_t)ADDR_HUD_CMD_SPRITES)  /* 0x004761B4 */
#define g_hudCmdSpritesEnd       ((uint8_t *)(uintptr_t)ADDR_HUD_CMD_SPRITES_END)  /* 0x004762CC */
#define g_hudColour              (*(uint8_t *)(uintptr_t)ADDR_HUD_MESSAGE_COLOUR)  /* 0x00507234 */
#define g_hudDirty               (*(int32_t *)(uintptr_t)ADDR_HUD_DIRTY)  /* 0x004FCF84 */
#define g_hudIndex               (*(int32_t *)(uintptr_t)ADDR_HUD_INDEX)  /* 0x004FCF50 */
#define g_hudPanelX              (*(int32_t *)(uintptr_t)ADDR_HUD_PANEL_X)  /* 0x00485328 */
#define g_hudSargeOffsets        ((int16_t *)(uintptr_t)ADDR_HUD_SARGE_OFFSETS)  /* 0x004766B0 */
#define g_hudSquadSlotXy         ((int16_t *)(uintptr_t)ADDR_HUD_SQUAD_SLOT_XY)  /* 0x004766C8 */
#define g_hudWidgetA             (*(struct AM2_HudTop **)(uintptr_t)ADDR_HUD_WIDGET_A)  /* 0x004FCF00 */
#define g_hudWidgetB             (*(AM2_HudPanel **)(uintptr_t)ADDR_HUD_WIDGET_B)  /* 0x004FCF54 */
#define g_hudWidgetC             (*(AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_C)  /* 0x004FCF4C */
#define g_hudWidgetTable         ((AM2_Widget **)(uintptr_t)ADDR_HUD_WIDGET_TABLE)  /* 0x004FCF04 */
#define g_iatGetTickCount        ((void *)(uintptr_t)ADDR_IAT_GET_TICK_COUNT)  /* 0x0046F084 */
#define g_iatIntersectRect       (*(AM2_IntersectRectFn *)(uintptr_t)ADDR_IAT_INTERSECT_RECT)  /* 0x0046F258 */
#define g_iatSmackClose          (*(am2_smack_close_fn *)(uintptr_t)ADDR_IAT_SMACK_CLOSE)  /* 0x0046F2C0 */
#define g_iatSmackDdtype         (*(am2_smack_ddtype_fn *)(uintptr_t)ADDR_IAT_SMACK_DDTYPE)  /* 0x0046F2CC */
#define g_iatSmackDoFrame        (*(am2_smack_frame_fn *)(uintptr_t)ADDR_IAT_SMACK_DO_FRAME)  /* 0x0046F2B4 */
#define g_iatSmackNextFrame      (*(am2_smack_frame_fn *)(uintptr_t)ADDR_IAT_SMACK_NEXT_FRAME)  /* 0x0046F2B8 */
#define g_iatSmackOpen           (*(am2_smack_open_fn *)(uintptr_t)ADDR_IAT_SMACK_OPEN)  /* 0x0046F2C8 */
#define g_iatSmackToBuffer       (*(am2_smack_tobuffer_fn *)(uintptr_t)ADDR_IAT_SMACK_TO_BUFFER)  /* 0x0046F2B0 */
#define g_iatSmackUseDsound      (*(am2_smack_use_dsound_fn *)(uintptr_t)ADDR_IAT_SMACK_USE_DSOUND)  /* 0x0046F2C4 */
#define g_iatSmackVolumepan      (*(am2_smack_volumepan_fn *)(uintptr_t)ADDR_IAT_SMACK_VOLUMEPAN)  /* 0x0046F2AC */
#define g_iatSmackWait           (*(am2_smack_ddtype_fn *)(uintptr_t)ADDR_IAT_SMACK_WAIT)  /* 0x0046F2BC */
#define g_infoOverlayOn          (*(int32_t *)(uintptr_t)ADDR_INFO_OVERLAY_ON)  /* 0x0048C7D8 */
#define g_inputSuppress          (*(int32_t *)(uintptr_t)ADDR_INPUT_SUPPRESS)  /* 0x00511E44 */
#define g_invulnerable           (*(int32_t *)(uintptr_t)ADDR_CHEAT_INVULNERABLE)  /* 0x00512358 */
#define g_itemHeaderSize         (*(int32_t *)(uintptr_t)ADDR_ITEM_HEADER_SIZE)  /* 0x0051307C */
#define g_itemTypeNames          ((char **)(uintptr_t)ADDR_ITEM_TYPE_NAMES)  /* 0x0048C480 */
#define g_iterCursor             (*(int32_t *)(uintptr_t)ADDR_ITER_CURSOR)  /* 0x00514F08 */
#define g_iterStamp              (*(uint32_t *)(uintptr_t)ADDR_ITER_STAMP)  /* 0x0051308C */
#define g_keyBindings            ((uint8_t *)(uintptr_t)ADDR_KEY_BINDINGS)  /* 0x004854BC */
#define g_keyBindingsEnd         ((uint8_t *)(uintptr_t)ADDR_KEY_BINDINGS_END)  /* 0x004854E6 */
#define g_keyDefaults            ((uint8_t *)(uintptr_t)ADDR_KEY_DEFAULTS)  /* 0x0048AE80 */
#define g_keyNameTable           ((AM2_KeyName *)(uintptr_t)ADDR_KEY_NAME_TABLE)  /* 0x0048AF28 */
#define g_keyNameTableEnd        ((uint8_t *)(uintptr_t)ADDR_KEY_NAME_TABLE_END)  /* 0x0048B220 */
#define g_keyPressed             ((int32_t *)(uintptr_t)ADDR_KEY_PRESSED)  /* 0x00512BD0 */
#define g_keyRepeat              ((uint32_t *)(uintptr_t)ADDR_KEY_REPEAT_AT)  /* 0x005127D0 */
#define g_keyrowPositions        ((int16_t *)(uintptr_t)ADDR_KEYROW_POSITIONS)  /* 0x0048AEC8 */
#define g_keysBufferA            ((void *)(uintptr_t)ADDR_KEYS_BUFFER_A)  /* 0x005125C8 */
#define g_keysBufferB            ((void *)(uintptr_t)ADDR_KEYS_BUFFER_B)  /* 0x005126C8 */
#define g_keyTable               (*(void **)(uintptr_t)ADDR_KEY_TABLE)  /* 0x00516150 */
#define g_keyTableN              (*(int32_t *)(uintptr_t)ADDR_KEY_TABLE_COUNT)  /* 0x00516148 */
#define g_kind7Box               ((const AM2_Rect *)(uintptr_t)ADDR_KIND7_BOX)  /* 0x00487420 */
#define g_kind7Count             ((int32_t *)(uintptr_t)ADDR_KIND7_COUNT)  /* 0x0051616C */
#define g_kind7Names             ((void **)(uintptr_t)ADDR_KIND7_NAMES)  /* 0x0050712C */
#define g_kindFrames             ((int32_t *)(uintptr_t)ADDR_KIND_FRAMES)  /* 0x0048BE30 */
#define g_lastMessage            (*(uint32_t *)(uintptr_t)ADDR_LAST_MESSAGE)  /* 0x004F9FE4 */
#define g_lastMsgChecksum        (*(uint32_t *)(uintptr_t)ADDR_LAST_MSG_CHECKSUM)  /* 0x004F48F4 */
#define g_lastMsgValue           (*(int32_t *)(uintptr_t)ADDR_LAST_MSG_VALUE)  /* 0x004F48F0 */
#define g_lastTickMs             (*(uint32_t *)(uintptr_t)ADDR_LAST_TICK_MS)  /* 0x00511E0C */
#define g_leaderFacing           (*(uint8_t *)(uintptr_t)ADDR_LEADER_FACING)  /* 0x00511E54 */
#define g_leaderPos              (*(AM2_Point *)(uintptr_t)ADDR_LEADER_POS)  /* 0x00511E50 */
#define g_leakCount              (*(int32_t *)(uintptr_t)ADDR_LEAK_COUNT)  /* 0x0050C344 */
#define g_leakRecords            (*(void **)(uintptr_t)ADDR_LEAK_RECORDS)  /* 0x0050C348 */
#define g_leakTotal              (*(int32_t *)(uintptr_t)ADDR_LEAK_TOTAL)  /* 0x0050C340 */
#define g_levelAttempt           (*(int32_t *)(uintptr_t)ADDR_LEVEL_ATTEMPT)  /* 0x00512330 */
#define g_levelCount             (*(int32_t *)(uintptr_t)ADDR_LEVEL_TABLE_COUNT)  /* 0x0065633C */
#define g_levelFlagE30           (*(int32_t *)(uintptr_t)ADDR_LEVEL_FLAG_E30)  /* 0x00511E30 */
#define g_levelId                (*(int32_t *)(uintptr_t)ADDR_LEVEL_ID)  /* 0x00511D98 */
#define g_levelIndex             (*(int32_t *)(uintptr_t)ADDR_LEVEL_INDEX)  /* 0x00511D9C */
#define g_levelSoundName         (*(char *)(uintptr_t)ADDR_LEVEL_SOUND_NAME)  /* 0x00511D58 */
#define g_levelStrA              ((char *)(uintptr_t)ADDR_LEVEL_STR_A)  /* 0x00511C48 */
#define g_levelStrB              ((char *)(uintptr_t)ADDR_LEVEL_STR_B)  /* 0x00511C88 */
#define g_levelStrC              (*(char *)(uintptr_t)ADDR_LEVEL_STR_C)  /* 0x00511D18 */
#define g_levelStrD              ((char *)(uintptr_t)ADDR_LEVEL_STR_D)  /* 0x00511CD8 */
#define g_levelTable             (*(struct AM2_Level **)(uintptr_t)ADDR_LEVEL_TABLE)  /* 0x00656338 */
#define g_levelTableCap          (*(int32_t *)(uintptr_t)ADDR_LEVEL_TABLE_CAP)  /* 0x00656340 */
#define g_listenerPos            (*(uint32_t *)(uintptr_t)ADDR_LISTENER_POS)  /* 0x00514E0C */
#define g_listenerPosPrev        (*(uint32_t *)(uintptr_t)ADDR_LISTENER_POS_PREV)  /* 0x00514E10 */
#define g_listInkHotSel          (*(uint8_t *)(uintptr_t)ADDR_LIST_INK_HOT_SEL)  /* 0x00502ACC */
#define g_liveBattleName         ((char *)(uintptr_t)ADDR_LIVE_BATTLE_NAME)  /* 0x00516035 */
#define g_livePlayerName         ((char *)(uintptr_t)ADDR_LIVE_PLAYER_NAME)  /* 0x00515FF4 */
#define g_loadPending            (*(int32_t *)(uintptr_t)ADDR_LOAD_PENDING)  /* 0x00511DD8 */
#define g_logpalette             ((uint8_t *)(uintptr_t)ADDR_LOGPALETTE)  /* 0x00477A60 */
#define g_logpaletteEntries      ((uint8_t *)(uintptr_t)ADDR_LOGPALETTE_ENTRIES)  /* 0x00477A64 */
#define g_looping                (*(int32_t *)(uintptr_t)ADDR_AUDIO_LOOPING)  /* 0x004FA45C */
#define g_mapBlock               ((char *)(uintptr_t)ADDR_MAP_BLOCK)  /* 0x00514D90 */
#define g_mapBounds              ((uint8_t *)(uintptr_t)ADDR_MAP_BOUNDS)  /* 0x00514DE8 */
#define g_mapBoundsBottom        (*(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_BOTTOM)  /* 0x00514E04 */
#define g_mapBoundsLeft          (*(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_LEFT)  /* 0x00514DF8 */
#define g_mapBoundsRight         (*(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_RIGHT)  /* 0x00514E00 */
#define g_mapBoundsTop           (*(int32_t *)(uintptr_t)ADDR_MAP_BOUNDS_TOP)  /* 0x00514DFC */
#define g_mapChecksum            (*(uint32_t *)(uintptr_t)ADDR_MAP_CHECKSUM_VAL)  /* 0x00511CCC */
#define g_mapDesc                ((struct AM2_MapDesc *)(uintptr_t)ADDR_MAP_DESC)  /* 0x00514F20 */
#define g_mapExtentShift         (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_SHIFT)  /* 0x00514DD8 */
#define g_mapExtentX             (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_X)  /* 0x00514DD0 */
#define g_mapExtentY             (*(int32_t *)(uintptr_t)ADDR_MAP_EXTENT_Y)  /* 0x00514DD4 */
#define g_mapFieldDescs          ((AM2_MapFieldDesc *)(uintptr_t)ADDR_MAP_FIELD_DESCS)  /* 0x00485FB8 */
#define g_mapName                ((char *)(uintptr_t)ADDR_OPT_MAP_NAME)  /* 0x004F9FEC */
#define g_mapName_               (*(char *)(uintptr_t)ADDR_MAP_NAME)  /* 0x00511A88 */
#define g_mapNameDefault         (*(char **)(uintptr_t)ADDR_MAP_NAME_DEFAULT)  /* 0x00485108 */
#define g_mapPadbitLayer         (*(uint8_t **)(uintptr_t)ADDR_MAP_PADBIT_LAYER)  /* 0x00514EC4 */
#define g_mapPadLayer            (*(uint8_t **)(uintptr_t)ADDR_MAP_PAD_LAYER)  /* 0x00514EC8 */
#define g_mapRowShift            (*(uint8_t *)(uintptr_t)ADDR_MAP_ROW_SHIFT)  /* 0x00514DE4 */
#define g_mapSprite              (*(struct AM2_Sprite **)(uintptr_t)ADDR_MAP_SURFACE)  /* 0x00514E90 */
#define g_mapTiles               (*(uint8_t **)(uintptr_t)ADDR_MAP_TILES)  /* 0x00514EB8 */
#define g_mapTilesH              (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_H)  /* 0x00514DE0 */
#define g_mapTilesW              (*(int32_t *)(uintptr_t)ADDR_MAP_TILES_W)  /* 0x00514DDC */
#define g_markSpriteCount        (*(int32_t *)(uintptr_t)ADDR_MARK_SPRITE_COUNT)  /* 0x0048CBB4 */
#define g_markSprites            (*(AM2_Sprite ***)(uintptr_t)ADDR_MARK_SPRITES)  /* 0x0048CBB0 */
#define g_medicHealPct           (*(int32_t *)(uintptr_t)ADDR_MEDIC_HEAL_PCT)  /* 0x006624F8 */
#define g_menuCursorDx           (*(int32_t *)(uintptr_t)ADDR_MENU_CURSOR_DX)  /* 0x004FCDBC */
#define g_menuCursorPrev         ((uint8_t *)(uintptr_t)ADDR_MENU_CURSOR_PREV)  /* 0x004FCDC8 */
#define g_menuCursorRect         ((uint8_t *)(uintptr_t)ADDR_MENU_CURSOR_RECT)  /* 0x004FCDD8 */
#define g_menuEnabled            (*(int32_t *)(uintptr_t)ADDR_MENU_ENABLED)  /* 0x004FCEF8 */
#define g_menuInk                (*(uint8_t **)(uintptr_t)ADDR_MENU_INK)  /* 0x004FCDB0 */
#define g_menuMsgList            (*(void **)(uintptr_t)ADDR_MENU_MSG_LIST)  /* 0x0051612C */
#define g_menuOverlayA           (*(struct AM2_Sprite **)(uintptr_t)ADDR_MENU_OVERLAY_A)  /* 0x004FCDA8 */
#define g_menuOverlayADx         (*(int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_A_DX)  /* 0x004FCDC0 */
#define g_menuOverlayAInk        (*(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_A_INK)  /* 0x004FCDB4 */
#define g_menuOverlayB           (*(struct AM2_Sprite **)(uintptr_t)ADDR_MENU_OVERLAY_B)  /* 0x004FCDAC */
#define g_menuOverlayBDx         (*(int32_t *)(uintptr_t)ADDR_MENU_OVERLAY_B_DX)  /* 0x004FCDC4 */
#define g_menuOverlayBInk        (*(uint8_t **)(uintptr_t)ADDR_MENU_OVERLAY_B_INK)  /* 0x004FCDB8 */
#define g_menuRequest            (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST)  /* 0x00511DC8 */
#define g_menuRequestSet         (*(int32_t *)(uintptr_t)ADDR_MENU_REQUEST_SET)  /* 0x00511DC4 */
#define g_menuRow                (*(int32_t *)(uintptr_t)ADDR_MENU_ROW)  /* 0x004FCAA8 */
#define g_menuRowStamp           (*(uint32_t *)(uintptr_t)ADDR_MENU_ROW_STAMP)  /* 0x004FCDF0 */
#define g_menuSprites            (*(struct AM2_Sprite **)(uintptr_t)ADDR_MENU_SPRITES)  /* 0x004FCAAC */
#define g_menuSpritesEnd         (*(struct AM2_Sprite **)(uintptr_t)ADDR_MENU_SPRITES_END)  /* 0x004FCDA4 */
#define g_messageBmpName         ((char *)(uintptr_t)ADDR_MESSAGE_BMP_NAME)  /* 0x005122A4 */
#define g_messageText            ((char *)(uintptr_t)ADDR_MESSAGE_TEXT)  /* 0x00511EA4 */
#define g_missileAnims           ((struct AM2_AnimTable *)(uintptr_t)ADDR_MISSILE_ANIMS)  /* 0x00654C90 */
#define g_missileBox             ((const AM2_Rect *)(uintptr_t)ADDR_MISSILE_BOX)  /* 0x00487B78 */
#define g_missileDefs            ((struct AM2_ItemType *)(uintptr_t)ADDR_MISSILE_DEFS)  /* 0x00662030 */
#define g_missileRowSpec         ((const struct AM2_RowSpec *)(uintptr_t)ADDR_MISSILE_ROW_SPEC)  /* 0x00487B88 */
#define g_missionRetry           (*(int32_t *)(uintptr_t)ADDR_MISSION_RETRY)  /* 0x0051232C */
#define g_mouseAcquired          (*(int32_t *)(uintptr_t)ADDR_DI_MOUSE_ACQUIRED)  /* 0x00512FE0 */
#define g_mouseActivity          (*(uint32_t *)(uintptr_t)ADDR_MOUSE_ACTIVITY)  /* 0x004854B0 */
#define g_mouseButton            (*(int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON)  /* 0x00485470 */
#define g_mouseButton1           (*(int32_t *)(uintptr_t)ADDR_MOUSE_BUTTON1)  /* 0x00485474 */
#define g_mouseChanged           (*(int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED)  /* 0x0048547C */
#define g_mouseChanged1          (*(int32_t *)(uintptr_t)ADDR_MOUSE_CHANGED1)  /* 0x00485480 */
#define g_mouseClaimed           (*(int32_t *)(uintptr_t)ADDR_MOUSE_CLAIMED)  /* 0x00485488 */
#define g_mouseDX                (*(int32_t *)(uintptr_t)ADDR_MOUSE_DX)  /* 0x00485458 */
#define g_mouseDY                (*(int32_t *)(uintptr_t)ADDR_MOUSE_DY)  /* 0x0048545C */
#define g_mouseDZ                (*(int32_t *)(uintptr_t)ADDR_MOUSE_DZ)  /* 0x00485460 */
#define g_mouseGrab              (*(int32_t *)(uintptr_t)ADDR_MOUSE_GRAB)  /* 0x004854B4 */
#define g_mouseMoved             (*(int32_t *)(uintptr_t)ADDR_MOUSE_MOVED)  /* 0x00485494 */
#define g_mousePress             ((uint32_t *)(uintptr_t)ADDR_MOUSE_PRESS)  /* 0x00485498 */
#define g_mousePress2Ms          (*(uint32_t *)(uintptr_t)ADDR_MOUSE_PRESS2_MS)  /* 0x004854A4 */
#define g_mousePressMs           (*(uint32_t *)(uintptr_t)ADDR_MOUSE_PRESS_MS)  /* 0x0048549C */
#define g_movieCount             (*(int32_t *)(uintptr_t)ADDR_MOVIE_COUNT)  /* 0x00512328 */
#define g_movieCurrent           (*(struct AM2_Movie **)(uintptr_t)ADDR_MOVIE_CURRENT)  /* 0x006568A0 */
#define g_movieDSound            (*(void **)(uintptr_t)ADDR_DSOUND)  /* 0x004FA46C */
#define g_movieNames             ((char **)(uintptr_t)ADDR_MOVIE_NAMES)  /* 0x0048AE98 */
#define g_moviePage              (*(int32_t *)(uintptr_t)ADDR_MOVIE_PAGE)  /* 0x0065A060 */
#define g_moviePaletteOwner      (*(uint8_t **)(uintptr_t)ADDR_MOVIE_PALETTE_OWNER)  /* 0x00477A58 */
#define g_movieToPlay            (*(char *)(uintptr_t)ADDR_MOVIE_TO_PLAY)  /* 0x00511B08 */
#define g_movieVtable            ((uint8_t *)(uintptr_t)ADDR_MOVIE_VTABLE)  /* 0x0046FAB4 */
#define g_mpMarkA                (*(AM2_Sprite **)(uintptr_t)ADDR_MP_MARK_A)  /* 0x00664594 */
#define g_mpMarkB                (*(AM2_Sprite **)(uintptr_t)ADDR_MP_MARK_B)  /* 0x00664598 */
#define g_mpMarkC                (*(AM2_Sprite **)(uintptr_t)ADDR_MP_MARK_C)  /* 0x0066459C */
#define g_mpMarkCols             (*(int32_t *)(uintptr_t)ADDR_MP_MARK_COLS)  /* 0x0048CBBC */
#define g_mpMarkGrid             (*(AM2_Sprite ***)(uintptr_t)ADDR_MP_MARK_GRID)  /* 0x0048CBB8 */
#define g_mpPanelSpritesA        ((AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_A)  /* 0x00516118 */
#define g_mpPanelSpritesB        ((AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_B)  /* 0x00515FA0 */
#define g_mpPanelSpritesBEnd     ((AM2_Sprite **)(uintptr_t)ADDR_MP_PANEL_SPRITES_B_END)  /* 0x00515FD4 */
#define g_mpRowCoords            ((uint8_t *)(uintptr_t)ADDR_MP_ROW_COORDS)  /* 0x0048C768 */
#define g_mpScriptChecksum       (*(uint32_t *)(uintptr_t)ADDR_MP_SCRIPT_CHKSUM_VAL)  /* 0x00511CD0 */
#define g_mpScriptDefault        (*(char **)(uintptr_t)ADDR_MP_SCRIPT_DEFAULT)  /* 0x0048510C */
#define g_mpScriptName           (*(char *)(uintptr_t)ADDR_MP_SCRIPT_NAME)  /* 0x00511C08 */
#define g_mpSession              (*(int32_t *)(uintptr_t)ADDR_MP_SESSION)  /* 0x00511DA0 */
#define g_mpTeamSprites          (*(AM2_Sprite ***)(uintptr_t)ADDR_MP_TEAM_SPRITES)  /* 0x006645A0 */
#define g_msgChat                ((uint8_t *)(uintptr_t)ADDR_MSG_CHAT)  /* 0x004FA910 */
#define g_msgColor               ((uint8_t *)(uintptr_t)ADDR_MSG_COLOR)  /* 0x004FC898 */
#define g_msgEndSetup            ((void *)(uintptr_t)ADDR_MSG_END_SETUP)  /* 0x004FC3A8 */
#define g_msgGamePause           ((uint8_t *)(uintptr_t)ADDR_MSG_GAME_PAUSE)  /* 0x004FAA50 */
#define g_msgGameReady           ((uint8_t *)(uintptr_t)ADDR_MSG_GAME_READY)  /* 0x004FAA28 */
#define g_msgGameStart           ((uint8_t *)(uintptr_t)ADDR_MSG_GAME_START)  /* 0x004FC5F0 */
#define g_msgKind01              ((uint8_t *)(uintptr_t)ADDR_MSG_KIND01)  /* 0x004FBF80 */
#define g_msgKind02              ((uint8_t *)(uintptr_t)ADDR_MSG_KIND02)  /* 0x004F48E8 */
#define g_msgKind04              ((uint8_t *)(uintptr_t)ADDR_MSG_KIND04)  /* 0x004FBB80 */
#define g_msgKind0c              ((void *)(uintptr_t)ADDR_MSG_KIND0C)  /* 0x004FC388 */
#define g_msgKind0d              ((uint8_t *)(uintptr_t)ADDR_MSG_KIND0D)  /* 0x004FC788 */
#define g_msgKind10              ((uint8_t *)(uintptr_t)ADDR_MSG_KIND10)  /* 0x004FAA38 */
#define g_msgListB               ((void *)(uintptr_t)ADDR_MSG_LIST_B)  /* 0x004F48C8 */
#define g_msgListDelayed         ((void *)(uintptr_t)ADDR_MSG_LIST_DELAYED)  /* 0x004F8780 */
#define g_msgListPool            ((void *)(uintptr_t)ADDR_MSG_LIST_POOL)  /* 0x0048D8E8 */
#define g_msgListSendq           ((void *)(uintptr_t)ADDR_MSG_LIST_SENDQ)  /* 0x0048D8D8 */
#define g_msgMap                 ((uint8_t *)(uintptr_t)ADDR_MSG_MAP)  /* 0x004FB770 */
#define g_msgReadyToLoad         ((uint8_t *)(uintptr_t)ADDR_MSG_READY_TO_LOAD)  /* 0x004FAA18 */
#define g_msgTeam                ((uint8_t *)(uintptr_t)ADDR_MSG_TEAM)  /* 0x004FC8A8 */
#define g_msgWantedFlags         (*(uint32_t *)(uintptr_t)ADDR_MSG_WANTED_FLAGS)  /* 0x004F8B98 */
#define g_nameBluescore          ((uint8_t *)(uintptr_t)ADDR_NAME_BLUESCORE)  /* 0x00487C6C */
#define g_nameBlueteamwins       ((uint8_t *)(uintptr_t)ADDR_NAME_BLUETEAMWINS)  /* 0x00478834 */
#define g_nameBluewins           ((uint8_t *)(uintptr_t)ADDR_NAME_BLUEWINS)  /* 0x0047886C */
#define g_nameGreenscore         ((uint8_t *)(uintptr_t)ADDR_NAME_GREENSCORE)  /* 0x00487C64 */
#define g_nameGreenteamwins      ((uint8_t *)(uintptr_t)ADDR_NAME_GREENTEAMWINS)  /* 0x00478850 */
#define g_nameGreenwins          ((uint8_t *)(uintptr_t)ADDR_NAME_GREENWINS)  /* 0x00478880 */
#define g_nameGreyscore          ((uint8_t *)(uintptr_t)ADDR_NAME_GREYSCORE)  /* 0x00487C70 */
#define g_nameGreyteamwins       ((uint8_t *)(uintptr_t)ADDR_NAME_GREYTEAMWINS)  /* 0x00478824 */
#define g_nameGreywins           ((uint8_t *)(uintptr_t)ADDR_NAME_GREYWINS)  /* 0x00478860 */
#define g_nameScoreLimit         ((uint8_t *)(uintptr_t)ADDR_NAME_SCORE_LIMIT)  /* 0x00487C60 */
#define g_nameTableBase          (*(struct AM2_NameRec **)(uintptr_t)ADDR_NAME_TABLE_BASE)  /* 0x00656344 */
#define g_nameTableCap           (*(int32_t *)(uintptr_t)ADDR_NAME_TABLE_CAP)  /* 0x0065634C */
#define g_nameTableCount         (*(int32_t *)(uintptr_t)ADDR_NAME_TABLE_COUNT)  /* 0x00656348 */
#define g_nameTanscore           ((uint8_t *)(uintptr_t)ADDR_NAME_TANSCORE)  /* 0x00487C68 */
#define g_nameTanteamwins        ((uint8_t *)(uintptr_t)ADDR_NAME_TANTEAMWINS)  /* 0x00478844 */
#define g_nameTanwins            ((uint8_t *)(uintptr_t)ADDR_NAME_TANWINS)  /* 0x00478878 */
#define g_netGame                (*(int32_t *)(uintptr_t)ADDR_NET_GAME)  /* 0x00511DD4 */
#define g_netSettleCount         (*(int32_t *)(uintptr_t)ADDR_NET_SETTLE_COUNT)  /* 0x0051234C */
#define g_nextUid                (*(int32_t *)(uintptr_t)ADDR_NEXT_UID)  /* 0x00511DF4 */
#define g_noBuffersLatch         (*(int32_t *)(uintptr_t)ADDR_COMM_NO_BUFFERS_LATCH)  /* 0x004F48E0 */
#define g_numberKeySlot          (*(int32_t *)(uintptr_t)ADDR_NUMBER_KEY_SLOT)  /* 0x004FCF90 */
#define g_objCap                 (*(int32_t *)(uintptr_t)ADDR_OBJ_CAPACITY)  /* 0x00514F00 */
#define g_objCount               (*(int32_t *)(uintptr_t)ADDR_OBJ_COUNT)  /* 0x00514F04 */
#define g_objCtxObj              (*(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ)  /* 0x005122CC */
#define g_objCtxObjA             (*(AM2_Object **)(uintptr_t)ADDR_OBJ_CTX_OBJ_A)  /* 0x005122C8 */
#define g_objCtxObjPrev          (*(void **)(uintptr_t)ADDR_OBJ_CTX_OBJ_PREV)  /* 0x005122D0 */
#define g_objCtxSet              (*(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET)  /* 0x00511E3C */
#define g_objCtxSetPrev          (*(int32_t *)(uintptr_t)ADDR_OBJ_CTX_SET_PREV)  /* 0x00511E40 */
#define g_objCtxVal              (*(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL)  /* 0x00511E28 */
#define g_objCtxValA             (*(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_A)  /* 0x00511E24 */
#define g_objCtxValPrev          (*(int32_t *)(uintptr_t)ADDR_OBJ_CTX_VAL_PREV)  /* 0x00511E2C */
#define g_objMapDesc             ((struct AM2_MapDesc *)(uintptr_t)ADDR_OBJ_MAP_DESC)  /* 0x00514F10 */
#define g_objMark                ((uint16_t *)(uintptr_t)ADDR_OBJ_MARK)  /* 0x00661E20 */
#define g_objMarkStamp           (*(uint16_t *)(uintptr_t)ADDR_OBJ_MARK_STAMP)  /* 0x00662020 */
#define g_objScriptCap           (*(int32_t *)(uintptr_t)ADDR_OBJ_SCRIPT_CAP)  /* 0x00516190 */
#define g_objScripts             (*(AM2_ObjScript **)(uintptr_t)ADDR_OBJ_SCRIPTS)  /* 0x00516188 */
#define g_objTable               (*(AM2_ObjEntry **)(uintptr_t)ADDR_OBJ_TABLE)  /* 0x00514F0C */
#define g_objTableRecords        ((uint8_t *)(uintptr_t)ADDR_OBJ_TABLE_RECORDS)  /* 0x004F9ACC */
#define g_opt4fd748              (*(int32_t *)(uintptr_t)ADDR_OPT_4FD748)  /* 0x004FD748 */
#define g_optBigMovies           (*(int32_t *)(uintptr_t)ADDR_OPT_BIG_MOVIES)  /* 0x005125C4 */
#define g_optDan                 ((uint8_t *)(uintptr_t)ADDR_OPT_DAN)  /* 0x004FD740 */
#define g_optDbg                 (*(int32_t *)(uintptr_t)ADDR_OPT_DBG)  /* 0x0050C358 */
#define g_optDf                  (*(int32_t *)(uintptr_t)ADDR_OPT_DF)  /* 0x0047894C */
#define g_optionTable            ((AM2_OptionRec *)(uintptr_t)ADDR_OPTION_TABLE)  /* 0x004865B8 */
#define g_optNoIntro             (*(int32_t *)(uintptr_t)ADDR_OPT_NO_INTRO)  /* 0x004FA038 */
#define g_optNoMovies            (*(int32_t *)(uintptr_t)ADDR_OPT_NO_MOVIES)  /* 0x0051259C */
#define g_optPeter               ((uint8_t *)(uintptr_t)ADDR_OPT_PETER)  /* 0x004FD744 */
#define g_optRob                 ((uint8_t *)(uintptr_t)ADDR_OPT_ROB)  /* 0x004FD73C */
#define g_optTracePf             (*(int32_t *)(uintptr_t)ADDR_OPT_TRACE_PF)  /* 0x0050C35C */
#define g_optTraceWin            ((uint8_t *)(uintptr_t)ADDR_OPT_TRACE_WIN)  /* 0x0050C354 */
#define g_originDX               (*(int32_t *)(uintptr_t)ADDR_ORIGIN_DX)  /* 0x00485330 */
#define g_originDY               (*(int32_t *)(uintptr_t)ADDR_ORIGIN_DY)  /* 0x00485334 */
#define g_ourLeaderUid           (*(uint32_t *)(uintptr_t)ADDR_OUR_LEADER_UID)  /* 0x00511E4C */
#define g_ourPoints              (*(int32_t *)(uintptr_t)ADDR_OUR_POINTS)  /* 0x004FCF9C */
#define g_ourSlot                (*(int32_t *)(uintptr_t)ADDR_OUR_SLOT)  /* 0x004FA904 */
#define g_overlayDirty           (*(int32_t *)(uintptr_t)ADDR_OVERLAY_DIRTY)  /* 0x00511DC0 */
#define g_overlayPalette         (*(void **)(uintptr_t)ADDR_OVERLAY_PALETTE)  /* 0x004FE1A4 */
#define g_packetBuffers          ((uint32_t *)(uintptr_t)ADDR_PACKET_BUFFERS)  /* 0x0048D978 */
#define g_packetRecords          ((uint8_t *)(uintptr_t)ADDR_PACKET_RECORDS)  /* 0x004F48F8 */
#define g_packetState            (*(int32_t *)(uintptr_t)ADDR_PACKET_STATE)  /* 0x004F877C */
#define g_packetThreadId         ((uint8_t *)(uintptr_t)ADDR_PACKET_THREAD_ID)  /* 0x004F8B90 */
#define g_padBitTable            ((int32_t *)(uintptr_t)ADDR_PAD_BIT_TABLE)  /* 0x00486444 */
#define g_padCount               (*(int32_t *)(uintptr_t)ADDR_PAD_COUNT)  /* 0x00511DF8 */
#define g_padNumbers             ((struct AM2_PadNumber *)(uintptr_t)ADDR_PAD_NUMBERS)  /* 0x0051F198 */
#define g_pads                   ((struct AM2_Pad *)(uintptr_t)ADDR_PADS)  /* 0x00516198 */
#define g_paintObject            (*(uint8_t * *)(uintptr_t)ADDR_PAINT_OBJECT)  /* 0x0065A058 */
#define g_paletteCopy            ((void *)(uintptr_t)ADDR_PALETTE_COPY)  /* 0x005022C8 */
#define g_paletteCycleCount      (*(int32_t *)(uintptr_t)ADDR_PALETTE_CYCLE_COUNT)  /* 0x00486164 */
#define g_paletteCycleIndex      (*(int32_t *)(uintptr_t)ADDR_PALETTE_CYCLE_INDEX)  /* 0x00486160 */
#define g_paletteCycleInterval   (*(uint32_t *)(uintptr_t)ADDR_PALETTE_CYCLE_INTERVAL)  /* 0x00486168 */
#define g_paletteCycleSeq        ((uint32_t *)(uintptr_t)ADDR_PALETTE_CYCLE_SEQ)  /* 0x00486138 */
#define g_paletteCycleStamp      (*(uint32_t *)(uintptr_t)ADDR_PALETTE_CYCLE_STAMP)  /* 0x00514F70 */
#define g_paletteGlyphs          ((uint8_t *)(uintptr_t)ADDR_PALETTE_GLYPHS)  /* 0x004FC998 */
#define g_particleKinds          ((struct AM2_ParticleKind *)(uintptr_t)ADDR_PARTICLE_KINDS)  /* 0x0048C7E4 */
#define g_pathArg                (*(char **)(uintptr_t)ADDR_AUDIO_PATH_ARG)  /* 0x004852D4 */
#define g_pathFrameMs            (*(int32_t *)(uintptr_t)ADDR_PATH_FRAME_MS)  /* 0x00654C34 */
#define g_pathFrameStamp         (*(int32_t *)(uintptr_t)ADDR_PATH_FRAME_STAMP)  /* 0x00654C38 */
#define g_pathGeneration         (*(uint16_t *)(uintptr_t)ADDR_PATH_GENERATION)  /* 0x00654C30 */
#define g_pathMaxNodes           (*(int32_t *)(uintptr_t)ADDR_PATH_MAX_NODES)  /* 0x0048788C */
#define g_pathMaxSearches        (*(int32_t *)(uintptr_t)ADDR_PATH_MAX_SEARCHES)  /* 0x00487890 */
#define g_pathResume             (*(int32_t *)(uintptr_t)ADDR_PATH_RESUME)  /* 0x00487828 */
#define g_pathResumeGoal         (*(int32_t *)(uintptr_t)ADDR_PATH_RESUME_GOAL)  /* 0x00554B80 */
#define g_pathResumeHead         (*(int32_t *)(uintptr_t)ADDR_PATH_RESUME_HEAD)  /* 0x00523DC8 */
#define g_pathRetryMs            (*(int32_t *)(uintptr_t)ADDR_PATH_RETRY_MS)  /* 0x00487894 */
#define g_pendingDelete          ((char *)(uintptr_t)ADDR_PENDING_CONFIRM)  /* 0x00659F58 */
#define g_perfFreq               ((int64_t *)(uintptr_t)ADDR_PERF_FREQ)  /* 0x00512350 */
#define g_perfPeriod             (*(double *)(uintptr_t)ADDR_PERF_PERIOD)  /* 0x00512570 */
#define g_perframeCountA         (*(int32_t *)(uintptr_t)ADDR_PERFRAME_COUNT_A)  /* 0x004F93B8 */
#define g_perframeCountB         (*(int32_t *)(uintptr_t)ADDR_PERFRAME_COUNT_B)  /* 0x004F93BC */
#define g_perfStart              (*(int64_t *)(uintptr_t)ADDR_PERF_START)  /* 0x00512578 */
#define g_period                 (*(uint32_t *)(uintptr_t)ADDR_AUDIO_PERIOD)  /* 0x004FA448 */
#define g_pickReach662450        (*(int32_t *)(uintptr_t)ADDR_PICK_REACH_662450)  /* 0x00662450 */
#define g_pickReach6624ec        (*(int32_t *)(uintptr_t)ADDR_PICK_REACH_6624EC)  /* 0x006624EC */
#define g_pickReach66275c        (*(int32_t *)(uintptr_t)ADDR_PICK_REACH_66275C)  /* 0x0066275C */
#define g_pickReach662894        (*(int32_t *)(uintptr_t)ADDR_PICK_REACH_662894)  /* 0x00662894 */
#define g_pickReach6628c8        (*(int32_t *)(uintptr_t)ADDR_PICK_REACH_6628C8)  /* 0x006628C8 */
/* A scalar of its own in the original: VC6 loads a lone short global with a
 * 16-bit `mov ax`, and an element of a larger object with a dword load, so the
 * relink build gives it its own symbol (relink.py maps am2_rl_* by the address
 * in the comment). Elsewhere it is a view into am2_rank_records. */
#if defined(AM2_RELINK)
#ifdef __cplusplus
extern "C"
#endif
const int16_t am2_rl_pillbox_trooper_health;  /* 0x00473E44 */
#define g_pillboxTrooperHealth   am2_rl_pillbox_trooper_health
#else
#define g_pillboxTrooperHealth   (*(const int16_t *)(uintptr_t)ADDR_PILLBOX_TROOPER_HEALTH)  /* 0x00473E44 */
#endif
#define g_pitch                  (*(int32_t *)(uintptr_t)ADDR_SCREEN_PITCH)  /* 0x00502AD0 */
#define g_placeFacing            (*(uint8_t *)(uintptr_t)ADDR_PLACE_FACING)  /* 0x004FCF8C */
#define g_placeFlag4fcf88        (*(int32_t *)(uintptr_t)ADDR_PLACE_FLAG_4FCF88)  /* 0x004FCF88 */
#define g_placementCap           (*(int32_t *)(uintptr_t)ADDR_PLACEMENT_CAP)  /* 0x00654C84 */
#define g_placementCount         (*(int32_t *)(uintptr_t)ADDR_PLACEMENT_COUNT)  /* 0x00654C80 */
#define g_placements             (*(AM2_Placement **)(uintptr_t)ADDR_PLACEMENTS)  /* 0x00654C7C */
#define g_playerMsg              (*(AM2_PlayerMsg *)(uintptr_t)ADDR_PLAYER_MSG)  /* 0x004FC3B8 */
#define g_playerRecords          ((uint8_t *)(uintptr_t)ADDR_PLAYER_RECORDS)  /* 0x004F1980 */
#define g_playerSlotMask         (*(uint32_t *)(uintptr_t)ADDR_PLAYER_SLOT_MASK)  /* 0x004F48DC */
#define g_pointerAction          (*(int32_t *)(uintptr_t)ADDR_POINTER_ACTION)  /* 0x005122E4 */
#define g_pointerF10             (*(int32_t *)(uintptr_t)ADDR_POINTER_F10)  /* 0x005122E8 */
#define g_pointerF14             (*(int32_t *)(uintptr_t)ADDR_POINTER_F14)  /* 0x005122EC */
#define g_pointerHoverUid        (*(uint32_t *)(uintptr_t)ADDR_POINTER_HOVER_UID)  /* 0x004854B8 */
#define g_pointerMode            (*(int32_t *)(uintptr_t)ADDR_POINTER_MODE)  /* 0x004FCF80 */
#define g_pointerModes           ((uint8_t *)(uintptr_t)ADDR_POINTER_MODES)  /* 0x004761B8 */
#define g_pointerOverlay         (*(int32_t *)(uintptr_t)ADDR_POINTER_OVERLAY)  /* 0x005122F4 */
#define g_pointerPick            (*(int32_t *)(uintptr_t)ADDR_POINTER_PICK)  /* 0x005122E0 */
#define g_pointRule              (*(uint32_t *)(uintptr_t)ADDR_POINT_RULE)  /* 0x00523DDC */
#define g_pointRuleArmy          (*(int32_t *)(uintptr_t)ADDR_POINT_RULE_ARMY)  /* 0x00523DD8 */
#define g_poseByClass            ((int32_t *)(uintptr_t)ADDR_POSE_BY_CLASS)  /* 0x00475180 */
#define g_presentEnabled         (*(int32_t *)(uintptr_t)ADDR_PRESENT_ENABLED)  /* 0x004FA030 */
#define g_prevKeys               (*(uint8_t **)(uintptr_t)ADDR_KEYS_PREV_PTR)  /* 0x005127CC */
#define g_primarySurface         (*(void **)(uintptr_t)ADDR_PRIMARY_SURFACE)  /* 0x00502AD4 */
#define g_radarColours           ((uint8_t *)(uintptr_t)ADDR_RADAR_COLOURS)  /* 0x004FCF5C */
#define g_randSeed               (*(uint32_t *)(uintptr_t)ADDR_RAND_SEED)  /* 0x0048CC1C */
#define g_rankExpTable           ((uint8_t *)(uintptr_t)ADDR_RANK_EXP_TABLE)  /* 0x00473DD8 */
#define g_rankRecords            ((AM2_RankRec *)(uintptr_t)ADDR_RANK_RECORDS)  /* 0x00473DC0 */
#define g_readFailed             (*(int32_t *)(uintptr_t)ADDR_AUDIO_READ_FAILED)  /* 0x004FA458 */
#define g_recordListCap          (*(int32_t *)(uintptr_t)ADDR_RECORD_LIST_CAP)  /* 0x00516138 */
#define g_recordListCount        (*(int32_t *)(uintptr_t)ADDR_RECORD_LIST_COUNT)  /* 0x0051613C */
#define g_recordListIndex        (*(void **)(uintptr_t)ADDR_RECORD_LIST_INDEX)  /* 0x00516154 */
#define g_recordLists            (*(struct AM2_RecordList ***)(uintptr_t)ADDR_RECORD_LISTS)  /* 0x00516140 */
#define g_recvScratch            ((void *)(uintptr_t)ADDR_RECV_SCRATCH)  /* 0x004F8790 */
#define g_regionCost             (*(uint8_t **)(uintptr_t)ADDR_REGION_COST)  /* 0x00514EFC */
#define g_regionCurrent          (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_CURRENT)  /* 0x00654C28 */
#define g_regionGeneration       (*(int32_t *)(uintptr_t)ADDR_REGION_GENERATION)  /* 0x00654C2C */
#define g_regionGoal             (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_GOAL)  /* 0x00523DC0 */
#define g_regionInsertPrev       (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_INSERT_PREV)  /* 0x00523D98 */
#define g_regionNeighbour        (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_NEIGHBOUR)  /* 0x00523D9C */
#define g_regionNext             (*(uint8_t **)(uintptr_t)ADDR_REGION_NEXT)  /* 0x00514EF4 */
#define g_regionOfCell           (*(uint8_t **)(uintptr_t)ADDR_REGION_OF_CELL)  /* 0x00514ECC */
#define g_regionOpenHead         (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_OPEN_HEAD)  /* 0x00523DC4 */
#define g_regionResumeGoalid     (*(int32_t *)(uintptr_t)ADDR_REGION_RESUME_GOALID)  /* 0x00523DCC */
#define g_regionResumeNode       (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_RESUME_NODE)  /* 0x0053C4C8 */
#define g_regions                (*(struct AM2_Region **)(uintptr_t)ADDR_REGIONS)  /* 0x00514EF0 */
#define g_regionSearchState      (*(int32_t *)(uintptr_t)ADDR_REGION_SEARCH_STATE)  /* 0x00487824 */
#define g_regionStamp            (*(uint8_t *)(uintptr_t)ADDR_REGION_STAMP)  /* 0x00514EF8 */
#define g_regionStride           (*(int16_t *)(uintptr_t)ADDR_REGION_STRIDE)  /* 0x00514EEC */
#define g_regionWalk             (*(struct AM2_Region **)(uintptr_t)ADDR_REGION_WALK)  /* 0x00523DD4 */
#define g_remapBright            (*(uint8_t **)(uintptr_t)ADDR_REMAP_BRIGHT)  /* 0x00507230 */
#define g_remapBrightBuf         ((uint8_t *)(uintptr_t)ADDR_REMAP_BRIGHT_BUF)  /* 0x004FE09C */
#define g_remapDark              (*(uint8_t **)(uintptr_t)ADDR_REMAP_DARK)  /* 0x004FE084 */
#define g_remapIdent             (*(uint8_t **)(uintptr_t)ADDR_REMAP_IDENTITY)  /* 0x004FD764 */
#define g_remapIdentityBuf       ((uint8_t *)(uintptr_t)ADDR_REMAP_IDENTITY_BUF)  /* 0x00507238 */
#define g_remapShades            ((uint8_t **)(uintptr_t)ADDR_REMAP_SHADES)  /* 0x004FE2B0 */
#define g_remapShadeStore        ((uint8_t *)(uintptr_t)ADDR_REMAP_SHADE_STORE)  /* 0x00502CEC */
#define g_remapTint              (*(uint8_t **)(uintptr_t)ADDR_REMAP_TINT)  /* 0x0047826C */
#define g_repairHealPct          (*(int32_t *)(uintptr_t)ADDR_REPAIR_HEAL_PCT)  /* 0x006628A0 */
#define g_resendBuf              ((uint8_t *)(uintptr_t)ADDR_RESEND_BUF)  /* 0x004FAE68 */
#define g_resendScratch          ((void *)(uintptr_t)ADDR_RESEND_SCRATCH)  /* 0x004FB780 */
#define g_respawnKindCount       (*(int32_t *)(uintptr_t)ADDR_RESPAWN_KIND_COUNT)  /* 0x00662924 */
#define g_respawnKindMask        ((uint32_t *)(uintptr_t)ADDR_RESPAWN_KIND_MASK)  /* 0x0048C530 */
#define g_respawnKinds           (*(int32_t **)(uintptr_t)ADDR_RESPAWN_KINDS)  /* 0x00662920 */
#define g_roachAnims             ((struct AM2_AnimTable *)(uintptr_t)ADDR_ROACH_ANIMS)  /* 0x00654C98 */
#define g_roachArmour            (*(int32_t *)(uintptr_t)ADDR_ROACH_ARMOUR)  /* 0x00487BB0 */
#define g_roachBiteBox           (*(AM2_Rect *)(uintptr_t)ADDR_ROACH_BITE_BOX)  /* 0x00487BD8 */
#define g_roachBox               ((const AM2_Rect *)(uintptr_t)ADDR_ROACH_BOX)  /* 0x00487BC8 */
#define g_roachDamage            (*(int32_t *)(uintptr_t)ADDR_ROACH_DAMAGE)  /* 0x00487BB4 */
#define g_roachForacc            (*(int32_t *)(uintptr_t)ADDR_ROACH_FORACC)  /* 0x00487BC0 */
#define g_roachForvel            (*(int32_t *)(uintptr_t)ADDR_ROACH_FORVEL)  /* 0x00487BB8 */
#define g_roachHealth            (*(int32_t *)(uintptr_t)ADDR_ROACH_HEALTH)  /* 0x00487BAC */
#define g_roachMark              ((uint16_t *)(uintptr_t)ADDR_ROACH_MARK)  /* 0x00656128 */
#define g_roachMarkStamp         (*(uint16_t *)(uintptr_t)ADDR_ROACH_MARK_STAMP)  /* 0x00656328 */
#define g_roachMasks             ((AM2_MaskRec *)(uintptr_t)ADDR_ROACH_MASK_COUNT)  /* 0x00654CA8 */
#define g_roachMaskDirs          (*(int32_t *)(uintptr_t)ADDR_ROACH_MASK_DIRECTIONS)  /* 0x00654CA0 */
#define g_roachReach             (*(float *)(uintptr_t)ADDR_ROACH_REACH)  /* 0x0046FAA8 */
#define g_roachRevacc            (*(int32_t *)(uintptr_t)ADDR_ROACH_REVACC)  /* 0x00487BC4 */
#define g_roachRevvel            (*(int32_t *)(uintptr_t)ADDR_ROACH_REVVEL)  /* 0x00487BBC */
#define g_roachRowSpec           ((const struct AM2_RowSpec *)(uintptr_t)ADDR_ROACH_ROW_SPEC)  /* 0x00487BE8 */
#define g_roachStartFrame        (*(int16_t *)(uintptr_t)ADDR_ROACH_START_FRAME)  /* 0x00487BFC */
#define g_rowLutDoubles          ((void *)(uintptr_t)ADDR_ROW_LUT_DOUBLES)  /* 0x004F9EDC */
#define g_rowPoolA               (*(AM2_RowPoolA *)(uintptr_t)ADDR_ROWPOOL_A_COUNT)  /* 0x00662938 */
#define g_rowPoolB               (*(AM2_RowPoolB *)(uintptr_t)ADDR_ROWPOOL_B_COUNT)  /* 0x006640C8 */
#define g_rulesChecksum          (*(uint32_t *)(uintptr_t)ADDR_RULES_CHECKSUM_VAL)  /* 0x00511CD4 */
#define g_rulesDirStr            ((uint8_t *)(uintptr_t)ADDR_RULES_DIR_STR)  /* 0x00485110 */
#define g_ruleUidA               (*(int32_t *)(uintptr_t)ADDR_RULE_UID_A)  /* 0x00510218 */
#define g_ruleUidB               (*(int32_t *)(uintptr_t)ADDR_RULE_UID_B)  /* 0x0051021C */
#define g_ruleUidC               (*(int32_t *)(uintptr_t)ADDR_RULE_UID_C)  /* 0x00510220 */
#define g_savedBattleName        (*(char *)(uintptr_t)ADDR_SAVED_BATTLE_NAME)  /* 0x005160D5 */
#define g_savedPlayerName        (*(char *)(uintptr_t)ADDR_SAVED_PLAYER_NAME)  /* 0x00516094 */
#define g_savedRect              ((AM2_Rect *)(uintptr_t)ADDR_MENU_SAVED_RECT)  /* 0x004FCA98 */
#define g_savedValid             (*(int32_t *)(uintptr_t)ADDR_MENU_SAVED_VALID)  /* 0x004FCEFC */
#define g_scenarioCount          (*(uint16_t *)(uintptr_t)ADDR_SCENARIO_COUNT)  /* 0x00656332 */
#define g_scenarios              (*(AM2_Scenario **)(uintptr_t)ADDR_SCENARIOS)  /* 0x00656334 */
#define g_scenarioUnread         (*(int32_t *)(uintptr_t)ADDR_SCENARIO_UNREAD)  /* 0x00656330 */
#define g_scoreLimit             (*(int32_t *)(uintptr_t)ADDR_SCORE_LIMIT)  /* 0x00515FF0 */
#define g_screenClip             ((uint8_t *)(uintptr_t)ADDR_SCREEN_CLIP)  /* 0x00485310 */
#define g_screenH                (*(int32_t *)(uintptr_t)ADDR_SCREEN_H)  /* 0x004852DC */
#define g_screenRect             (*(AM2_Rect *)(uintptr_t)ADDR_SCREEN_RECT)  /* 0x00485330 */
#define g_screenW                (*(int32_t *)(uintptr_t)ADDR_SCREEN_W)  /* 0x004852D8 */
#define g_scriptConditions       (*(AM2_ScriptCond **)(uintptr_t)ADDR_SCRIPT_CONDITIONS)  /* 0x00510214 */
#define g_scriptContext          ((void *)(uintptr_t)ADDR_SCRIPT_CONTEXT)  /* 0x00656478 */
#define g_scriptKindNames        ((uint8_t *)(uintptr_t)ADDR_SCRIPT_KIND_NAMES)  /* 0x00487C74 */
#define g_scriptNameCap          (*(int32_t *)(uintptr_t)ADDR_SCRIPT_NAME_CAP)  /* 0x00656460 */
#define g_scriptNameCount        (*(int32_t *)(uintptr_t)ADDR_SCRIPT_NAME_COUNT)  /* 0x00656464 */
#define g_scriptNames            (*(void **)(uintptr_t)ADDR_SCRIPT_NAMES)  /* 0x00656468 */
#define g_scriptReload           (*(int32_t *)(uintptr_t)ADDR_SCRIPT_RELOAD)  /* 0x00511DCC */
#define g_scriptReloading        (*(int32_t *)(uintptr_t)ADDR_SCRIPT_RELOADING)  /* 0x0050C350 */
#define g_scriptReloadPath       ((char *)(uintptr_t)ADDR_SCRIPT_RELOAD_PATH)  /* 0x00511BC8 */
#define g_scriptStateFlag        (*(int32_t *)(uintptr_t)ADDR_SCRIPT_STATE_FLAG)  /* 0x00511DFC */
#define g_scriptUnknownStr       ((uint8_t *)(uintptr_t)ADDR_SCRIPT_UNKNOWN_STR)  /* 0x00488258 */
#define g_scriptWordBuf          ((char *)(uintptr_t)ADDR_SCRIPT_WORD_BUF)  /* 0x00656354 */
#define g_secondDeadline         (*(int32_t *)(uintptr_t)ADDR_SECOND_DEADLINE)  /* 0x005122F8 */
#define g_secondRect             ((int32_t *)(uintptr_t)ADDR_SECOND_RECT)  /* 0x00514E34 */
#define g_secondRectPrev         (*(AM2_Rect *)(uintptr_t)ADDR_SECOND_RECT_PREV)  /* 0x00514E44 */
#define g_selectedCount          (*(int32_t *)(uintptr_t)ADDR_SELECTED_COUNT)  /* 0x0051230C */
#define g_selectedItems          (*(uint32_t **)(uintptr_t)ADDR_SELECTED_ITEMS)  /* 0x00512310 */
#define g_selectedUids           ((struct AM2_UidList *)(uintptr_t)ADDR_SELECTED_UIDS)  /* 0x00512308 */
#define g_sendRoll               (*(int32_t *)(uintptr_t)ADDR_SEND_ROLL)  /* 0x004F8BA8 */
#define g_seqAdvanceMs           (*(uint32_t *)(uintptr_t)ADDR_SEQ_ADVANCE_MS)  /* 0x0048CB94 */
#define g_seqCtxA                ((struct AM2_SeqCtx *)(uintptr_t)ADDR_SEQ_CTX_A)  /* 0x00664580 */
#define g_seqCtxB                ((struct AM2_SeqCtx *)(uintptr_t)ADDR_SEQ_CTX_B)  /* 0x006640B0 */
#define g_seqEmitMs              (*(uint32_t *)(uintptr_t)ADDR_SEQ_EMIT_MS)  /* 0x0048CBE4 */
#define g_seqGridRows            ((int32_t *)(uintptr_t)ADDR_SEQ_GRID_ROWS)  /* 0x0048CB88 */
#define g_seqK4DriftX            (*(int32_t *)(uintptr_t)ADDR_SEQ_K4_DRIFT_X)  /* 0x0048CBD8 */
#define g_seqK4DriftY            (*(int32_t *)(uintptr_t)ADDR_SEQ_K4_DRIFT_Y)  /* 0x0048CBDC */
#define g_seqK4Hold              ((uint32_t *)(uintptr_t)ADDR_SEQ_K4_HOLD)  /* 0x0048CBC8 */
#define g_seqK4Rise              (*(int16_t *)(uintptr_t)ADDR_SEQ_K4_RISE)  /* 0x0048CBE0 */
#define g_seqK4StepMs            (*(int32_t *)(uintptr_t)ADDR_SEQ_K4_STEP_MS)  /* 0x0048CBC4 */
#define g_seqSprite5Count        (*(int32_t *)(uintptr_t)ADDR_SEQ_SPRITE_5_COUNT)  /* 0x0048CBAC */
#define g_seqSprites5            (*(struct AM2_Sprite ***)(uintptr_t)ADDR_SEQ_SPRITES_5)  /* 0x0048CBA8 */
#define g_seqSprites7            (*(struct AM2_Sprite ***)(uintptr_t)ADDR_SEQ_SPRITES_7)  /* 0x0048CB98 */
#define g_seqTailFrames          (*(int32_t *)(uintptr_t)ADDR_SEQ_TAIL_FRAMES)  /* 0x0048CB90 */
#define g_sessionList            (*(void **)(uintptr_t)ADDR_SESSION_LIST)  /* 0x004FA908 */
#define g_sessionObject          (*(void **)(uintptr_t)ADDR_SESSION_OBJECT)  /* 0x00516130 */
#define g_shakeAmp               (*(int32_t *)(uintptr_t)ADDR_SHAKE_AMPLITUDE)  /* 0x00514E78 */
#define g_shakePhaseX            (*(float *)(uintptr_t)ADDR_SHAKE_PHASE_X)  /* 0x00514E68 */
#define g_shakePhaseY            (*(float *)(uintptr_t)ADDR_SHAKE_PHASE_Y)  /* 0x00514E70 */
#define g_shakePresets           ((Preset *)(uintptr_t)ADDR_SHAKE_PRESETS)  /* 0x00486170 */
#define g_shakeStepX             (*(int32_t *)(uintptr_t)ADDR_SHAKE_STEP_X)  /* 0x00514E6C */
#define g_shakeStepY             (*(int32_t *)(uintptr_t)ADDR_SHAKE_STEP_Y)  /* 0x00514E74 */
#define g_shakeTime              (*(int32_t *)(uintptr_t)ADDR_SHAKE_TIME)  /* 0x00514E64 */
#define g_sightBlockByDir        ((AM2_SightDirRec *)(uintptr_t)ADDR_SIGHT_BLOCK_BY_DIR)  /* 0x004F8FB8 */
#define g_sightGeneration        (*(int32_t *)(uintptr_t)ADDR_SIGHT_GENERATION)  /* 0x004F93C0 */
#define g_sightRangeWant         (*(double *)(uintptr_t)ADDR_SIGHT_RANGE_WANT)  /* 0x0046F2F8 */
#define g_slotHeadings           ((uint8_t *)(uintptr_t)ADDR_SLOT_HEADINGS)  /* 0x0065A068 */
#define g_slotIsVehicle          ((int32_t *)(uintptr_t)ADDR_SLOT_IS_VEHICLE)  /* 0x0065A1A8 */
#define g_slotPositions          ((AM2_Point *)(uintptr_t)ADDR_SLOT_POSITIONS)  /* 0x0065A0A8 */
#define g_slotRecs               ((struct AM2_SlotRec *)(uintptr_t)ADDR_SLOT_RECS)  /* 0x0048BD90 */
#define g_slowMachine            (*(int32_t *)(uintptr_t)ADDR_SLOW_MACHINE)  /* 0x005125C0 */
#define g_soldierAnims           ((struct AM2_AnimTable *)(uintptr_t)ADDR_SOLDIER_ANIMS)  /* 0x00659F00 */
#define g_soldierNames           ((struct AM2_SoldierName *)(uintptr_t)ADDR_SOLDIER_NAMES)  /* 0x00489BF8 */
#define g_soundReady             (*(int32_t *)(uintptr_t)ADDR_MOVIE_SOUND_READY)  /* 0x006598A8 */
#define g_soundSlots             ((struct AM2_SoundSlot *)(uintptr_t)ADDR_SOUND_SLOTS)  /* 0x004FA040 */
#define g_soundSlotsEnd          ((struct AM2_SoundSlot *)(uintptr_t)ADDR_SOUND_SLOTS_END)  /* 0x004FA3C0 */
#define g_spawnExtra6622bc       (*(int32_t *)(uintptr_t)ADDR_SPAWN_EXTRA_6622BC)  /* 0x006622BC */
#define g_spawnExtra6628d4       (*(int32_t *)(uintptr_t)ADDR_SPAWN_EXTRA_6628D4)  /* 0x006628D4 */
#define g_spawnKindTable         ((uint8_t *)(uintptr_t)ADDR_SPAWN_KIND_TABLE)  /* 0x0066205C */
#define g_spiralDx               ((int32_t *)(uintptr_t)ADDR_SPIRAL_DX)  /* 0x0048782C */
#define g_spiralDy               ((int32_t *)(uintptr_t)ADDR_SPIRAL_DY)  /* 0x0048783C */
#define g_spiralStep             ((uint8_t *)(uintptr_t)ADDR_SPIRAL_STEP)  /* 0x00485340 */
#define g_spriteFile             (*(am2_FILE **)(uintptr_t)ADDR_SPRITE_FILE)  /* 0x006598B8 */
#define g_spriteGrid             (*(struct AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_GRID)  /* 0x0048CA40 */
#define g_spriteGridCols         (*(int32_t *)(uintptr_t)ADDR_SPRITE_GRID_COLS)  /* 0x0048CA3C */
#define g_spriteGridRows         ((int32_t *)(uintptr_t)ADDR_SPRITE_GRID_ROWS)  /* 0x0048CA38 */
#define g_spriteGroups           ((AM2_SpriteGroup *)(uintptr_t)ADDR_SPRITE_GROUPS)  /* 0x0048C7E0 */
#define g_spriteGroupsBEnd       ((uint8_t *)(uintptr_t)ADDR_SPRITE_GROUPS_B_END)  /* 0x0048CA08 */
#define g_spriteGroupsC          ((struct AM2_SpriteGroupC *)(uintptr_t)ADDR_SPRITE_GROUPS_C)  /* 0x0048CA08 */
#define g_spriteGroupsCEnd       ((uint8_t *)(uintptr_t)ADDR_SPRITE_GROUPS_C_END)  /* 0x0048CA38 */
#define g_spriteGroupsEnd        ((uint8_t *)(uintptr_t)ADDR_SPRITE_GROUPS_END)  /* 0x0048C8D0 */
#define g_spriteList             (*(struct AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_LIST)  /* 0x004F96C8 */
#define g_spriteListCap          (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_CAP)  /* 0x004F96C0 */
#define g_spriteListN            (*(int32_t *)(uintptr_t)ADDR_SPRITE_LIST_COUNT)  /* 0x004F96C4 */
#define g_spriteRegCap           (*(int32_t *)(uintptr_t)ADDR_SPRITE_REG_CAP)  /* 0x006598BC */
#define g_spriteRegCount         (*(int32_t *)(uintptr_t)ADDR_SPRITE_REG_COUNT)  /* 0x006598C0 */
#define g_spriteRegPairs         (*(void **)(uintptr_t)ADDR_SPRITE_REG_PAIRS)  /* 0x006598C8 */
#define g_spriteSetDirs          ((char **)(uintptr_t)ADDR_SPRITE_SET_DIRS)  /* 0x00489554 */
#define g_spriteSetShared        ((struct AM2_SpriteSet *)(uintptr_t)ADDR_SPRITE_SET_SHARED)  /* 0x00510230 */
#define g_spriteSetThird         ((struct AM2_SpriteSet *)(uintptr_t)ADDR_SPRITE_SET_THIRD)  /* 0x00511250 */
#define g_spriteSetTitle         ((struct AM2_SpriteSet *)(uintptr_t)ADDR_SPRITE_SET_TITLE)  /* 0x00510A40 */
#define g_spriteTable            (*(AM2_Sprite ***)(uintptr_t)ADDR_SPRITE_TABLE)  /* 0x006598C4 */
#define g_startupColours         ((uint8_t *)(uintptr_t)ADDR_STARTUP_COLOURS)  /* 0x00512340 */
#define g_startupColoursB        ((uint8_t *)(uintptr_t)ADDR_STARTUP_COLOURS_B)  /* 0x006645A4 */
#define g_state0Tick             (*(uint32_t *)(uintptr_t)ADDR_STATE0_TICK)  /* 0x00511A60 */
#define g_stateActions           ((AM2_StateAction *)(uintptr_t)ADDR_STATE_ACTIONS)  /* 0x0048654C */
#define g_stateArg               (*(int32_t *)(uintptr_t)ADDR_GAME_STATE_ARG)  /* 0x00511DB4 */
#define g_stateEntered           (*(int32_t *)(uintptr_t)ADDR_STATE_ENTERED)  /* 0x00511DA8 */
#define g_stateEnterOnce         (*(int32_t *)(uintptr_t)ADDR_STATE_ENTER_ONCE)  /* 0x00511DD0 */
#define g_stateMovie             (*(struct AM2_Movie **)(uintptr_t)ADDR_STATE_MOVIE)  /* 0x00515F98 */
#define g_statePending           (*(int32_t *)(uintptr_t)ADDR_STATE_PENDING)  /* 0x00511DAC */
#define g_stateWanted            (*(int32_t *)(uintptr_t)ADDR_STATE_WANTED)  /* 0x00511DB0 */
#define g_stepFacingSweep        ((int32_t *)(uintptr_t)ADDR_STEP_FACING_SWEEP)  /* 0x00489E00 */
#define g_strAviDir              (*(char **)(uintptr_t)ADDR_STR_AVI_DIR)  /* 0x004852C8 */
#define g_strBool0               ((char *)(uintptr_t)ADDR_STR_BOOL_0)  /* 0x0047795C */
#define g_strBool1               ((char *)(uintptr_t)ADDR_STR_BOOL_1)  /* 0x00477978 */
#define g_strBoolFalseCap        ((char *)(uintptr_t)ADDR_STR_BOOL_FALSE_CAP)  /* 0x00477968 */
#define g_strBoolFalseLow        ((char *)(uintptr_t)ADDR_STR_BOOL_FALSE_LOW)  /* 0x00477970 */
#define g_strBoolLf              ((char *)(uintptr_t)ADDR_STR_BOOL_LF)  /* 0x00477960 */
#define g_strBoolLt              ((char *)(uintptr_t)ADDR_STR_BOOL_LT)  /* 0x0047797C */
#define g_strBoolTrueCap         ((char *)(uintptr_t)ADDR_STR_BOOL_TRUE_CAP)  /* 0x00477984 */
#define g_strBoolTrueLow         ((char *)(uintptr_t)ADDR_STR_BOOL_TRUE_LOW)  /* 0x0047798C */
#define g_strBoolUf              ((char *)(uintptr_t)ADDR_STR_BOOL_UF)  /* 0x00477964 */
#define g_strBoolUt              ((char *)(uintptr_t)ADDR_STR_BOOL_UT)  /* 0x00477980 */
#define g_strCapsGuarNo          ((char *)(uintptr_t)ADDR_STR_CAPS_GUAR_NO)  /* 0x00475354 */
#define g_strCapsGuarYes         ((char *)(uintptr_t)ADDR_STR_CAPS_GUAR_YES)  /* 0x0047537C */
#define g_streamVolume           (*(int32_t *)(uintptr_t)ADDR_STREAM_VOLUME)  /* 0x0051231C */
#define g_strFalse               ((char *)(uintptr_t)ADDR_STR_FALSE)  /* 0x00475C18 */
#define g_strLobbyAsHost         ((char *)(uintptr_t)ADDR_STR_LOBBY_AS_HOST)  /* 0x00475504 */
#define g_strLobbyAsSlave        ((char *)(uintptr_t)ADDR_STR_LOBBY_AS_SLAVE)  /* 0x004754DC */
#define g_strTrue                ((char *)(uintptr_t)ADDR_STR_TRUE)  /* 0x00475C20 */
#define g_subState               (*(int32_t *)(uintptr_t)ADDR_MENU_MODE)  /* 0x00511DBC */
#define g_surfaceLocked          (*(int32_t *)(uintptr_t)ADDR_SURFACE_LOCKED)  /* 0x004FDF80 */
#define g_svarBlue               (*(int32_t *)(uintptr_t)ADDR_SVAR_BLUE)  /* 0x00656454 */
#define g_svarBluescore          (*(int32_t *)(uintptr_t)ADDR_SVAR_BLUESCORE)  /* 0x00656490 */
#define g_svarDifficulty         (*(int32_t *)(uintptr_t)ADDR_SVAR_DIFFICULTY)  /* 0x0065649C */
#define g_svarGreen              (*(int32_t *)(uintptr_t)ADDR_SVAR_GREEN)  /* 0x00656484 */
#define g_svarGreenscore         (*(int32_t *)(uintptr_t)ADDR_SVAR_GREENSCORE)  /* 0x00656488 */
#define g_svarGrey               (*(int32_t *)(uintptr_t)ADDR_SVAR_GREY)  /* 0x0065646C */
#define g_svarGreyscore          (*(int32_t *)(uintptr_t)ADDR_SVAR_GREYSCORE)  /* 0x00656494 */
#define g_svarId15               (*(int32_t *)(uintptr_t)ADDR_SVAR_ID15)  /* 0x00656474 */
#define g_svarMe                 (*(int32_t *)(uintptr_t)ADDR_SVAR_ME)  /* 0x00656458 */
#define g_svarNumGreen           (*(int32_t *)(uintptr_t)ADDR_SVAR_NUMGREEN)  /* 0x00656350 */
#define g_svarSystemSpeed        (*(int32_t *)(uintptr_t)ADDR_SVAR_SYSTEMSPEED)  /* 0x00656470 */
#define g_svarTan                (*(int32_t *)(uintptr_t)ADDR_SVAR_TAN)  /* 0x00656498 */
#define g_svarTanscore           (*(int32_t *)(uintptr_t)ADDR_SVAR_TANSCORE)  /* 0x0065648C */
#define g_sweepDamage            (*(int32_t *)(uintptr_t)ADDR_SWEEP_DAMAGE)  /* 0x00662908 */
#define g_sweepDistance          (*(int32_t *)(uintptr_t)ADDR_SWEEP_DISTANCE)  /* 0x006628FC */
#define g_systemPalette_         ((uint8_t *)(uintptr_t)ADDR_SYSTEM_PALETTE)  /* 0x006564A0 */
#define g_throttleDeadline       (*(int32_t *)(uintptr_t)ADDR_THROTTLE_DEADLINE)  /* 0x00659EF8 */
#define g_tickIntervalMs         (*(int32_t *)(uintptr_t)ADDR_TICK_INTERVAL_MS)  /* 0x00485104 */
#define g_tileAttrs              (*(uint8_t **)(uintptr_t)ADDR_TILE_ATTRS)  /* 0x00514EBC */
#define g_tileCover              (*(uint8_t **)(uintptr_t)ADDR_TILE_COVER)  /* 0x00514EE8 */
#define g_tileFlags              (*(uint8_t **)(uintptr_t)ADDR_TILE_FLAGS)  /* 0x00514ED0 */
#define g_tileKind               (*(uint8_t **)(uintptr_t)ADDR_TILE_KIND)  /* 0x00514ED4 */
#define g_tileLineBuf            ((uint16_t *)(uintptr_t)ADDR_TILE_LINE_BUF)  /* 0x00523DE0 */
#define g_tilemaskNeighbours     ((int32_t *)(uintptr_t)ADDR_TILEMASK_NEIGHBOURS)  /* 0x00554B84 */
#define g_tileNeighbours         ((int32_t *)(uintptr_t)ADDR_TILE_NEIGHBOURS)  /* 0x00654BD8 */
#define g_tileRevealGrids        ((void **)(uintptr_t)ADDR_TILE_REVEAL_GRIDS)  /* 0x00514ED8 */
#define g_tileRing4              ((int32_t *)(uintptr_t)ADDR_TILE_RING4)  /* 0x00554B70 */
#define g_tileRing8              ((int32_t *)(uintptr_t)ADDR_TILE_RING8)  /* 0x0053C480 */
#define g_tilesetPalettes        ((uint8_t *)(uintptr_t)ADDR_TILESET_PALETTES)  /* 0x00503108 */
#define g_tilesetPath            ((char *)(uintptr_t)ADDR_MAP_FOLDER)  /* 0x00511AC8 */
#define g_tilesetReserve         (*(int32_t *)(uintptr_t)ADDR_TILESET_RESERVE)  /* 0x00511CC8 */
#define g_tileShiftLog           (*(int32_t *)(uintptr_t)ADDR_TILE_SHIFT_LOG)  /* 0x00514E9C */
#define g_tileStep8              ((int32_t *)(uintptr_t)ADDR_TILE_STEP8)  /* 0x00523DA0 */
#define g_timeoutLoggedAt        (*(uint32_t *)(uintptr_t)ADDR_TIMEOUT_LOGGED_AT)  /* 0x004FC8B8 */
#define g_timerBlock             (*(AM2_TimerBlock *)(uintptr_t)ADDR_TIMER_COUNT)  /* 0x0050C36C */
#define g_timerRunning           (*(int32_t *)(uintptr_t)ADDR_AUDIO_TIMER_RUN)  /* 0x004FA464 */
#define g_timerTable             ((uint8_t *)(uintptr_t)ADDR_TIMER_TABLE)  /* 0x0050C370 */
#define g_timerTableIdEnd        ((uint8_t *)(uintptr_t)ADDR_TIMER_TABLE_ID_END)  /* 0x005101FC */
#define g_traceVeh               (*(int32_t *)(uintptr_t)ADDR_OPT_TRACE_VEH)  /* 0x0050C360 */
#define g_trigCos                ((float *)(uintptr_t)ADDR_TRIG_COS)  /* 0x00515784 */
#define g_trigSin                ((float *)(uintptr_t)ADDR_TRIG_SIN)  /* 0x00514F80 */
#define g_trooperBox             ((const AM2_Rect *)(uintptr_t)ADDR_TROOPER_BOX)  /* 0x00489850 */
#define g_trooperClassValue      ((int32_t *)(uintptr_t)ADDR_TROOPER_CLASS_VALUE)  /* 0x00489840 */
#define g_trooperRowSpec         ((const struct AM2_RowSpec *)(uintptr_t)ADDR_TROOPER_ROW_SPEC)  /* 0x00489860 */
#define g_turnRepeatMs           (*(int32_t *)(uintptr_t)ADDR_TURN_REPEAT_MS)  /* 0x00659F48 */
#define g_turretAnims            ((struct AM2_AnimTable *)(uintptr_t)ADDR_TURRET_ANIMS)  /* 0x0065A2A8 */
#define g_type2List              (*(AM2_PtrList **)(uintptr_t)ADDR_TYPE2_ACTION_LIST)  /* 0x004F9ED0 */
#define g_uidCounter             ((uint32_t *)(uintptr_t)ADDR_UID_COUNTERS)  /* 0x00511DE0 */
#define g_uidRemap               (*(uint32_t **)(uintptr_t)ADDR_UID_REMAP)  /* 0x00513088 */
#define g_uidRemapCap            (*(int32_t *)(uintptr_t)ADDR_UID_REMAP_CAP)  /* 0x00513080 */
#define g_uidRemapCount          (*(int32_t *)(uintptr_t)ADDR_UID_REMAP_COUNT)  /* 0x00513084 */
#define g_unitClassNames         ((char **)(uintptr_t)ADDR_UNIT_CLASS_NAMES)  /* 0x00489B44 */
#define g_unitNameSarge          (*(char **)(uintptr_t)ADDR_UNIT_NAME_SARGE)  /* 0x00489B40 */
#define g_unitTypes              ((struct AM2_UnitType *)(uintptr_t)ADDR_UNIT_TYPES)  /* 0x00487898 */
#define g_unread50c34c           (*(int32_t *)(uintptr_t)ADDR_UNREAD_50C34C)  /* 0x0050C34C */
#define g_unused662288           (*(int32_t *)(uintptr_t)ADDR_UNUSED_662288)  /* 0x00662288 */
#define g_validBytes             (*(uint32_t *)(uintptr_t)ADDR_AUDIO_VALID_BYTES)  /* 0x004FA454 */
#define g_variationBlocks        ((uint8_t *)(uintptr_t)ADDR_VARIATION_BLOCKS)  /* 0x004FE2C0 */
#define g_variationEnd           ((uint8_t *)(uintptr_t)ADDR_VARIATION_END)  /* 0x005022C0 */
#define g_variationTable         ((uint8_t **)(uintptr_t)ADDR_VARIATION_TABLE)  /* 0x00507130 */
#define g_vehicleAnims           ((struct AM2_AnimTable *)(uintptr_t)ADDR_VEHICLE_ANIMS)  /* 0x00661DF0 */
#define g_vehicleBox             ((const AM2_Rect *)(uintptr_t)ADDR_VEHICLE_BOX)  /* 0x0048BE10 */
#define g_vehicleDefCap          (*(int32_t *)(uintptr_t)ADDR_VEHICLE_DEF_CAP)  /* 0x0066202C */
#define g_vehicleDefCount        (*(int32_t *)(uintptr_t)ADDR_VEHICLE_DEF_COUNT)  /* 0x00662028 */
#define g_vehicleDefs            (*(uint8_t **)(uintptr_t)ADDR_VEHICLE_DEFS)  /* 0x00662024 */
#define g_vehicleHeightByKind    ((int32_t *)(uintptr_t)ADDR_VEHICLE_HEIGHT_BY_KIND)  /* 0x0048BDF8 */
#define g_vehicleMasks           ((AM2_MaskRec *)(uintptr_t)ADDR_VEHICLE_MASK_COUNT)  /* 0x0065A2F0 */
#define g_vehicleMaskDirs        ((int32_t *)(uintptr_t)ADDR_VEHICLE_MASK_DIRECTIONS)  /* 0x0065A2D8 */
#define g_vehicleNames           ((char **)(uintptr_t)ADDR_VEHICLE_NAMES)  /* 0x0048BE50 */
#define g_vehicleRowSpec         ((const struct AM2_RowSpec *)(uintptr_t)ADDR_VEHICLE_ROW_SPEC)  /* 0x0048BE20 */
#define g_viewColourCopy         (*(uint8_t *)(uintptr_t)ADDR_VIEW_COLOUR_COPY)  /* 0x004FCFA8 */
#define g_viewFarX               (*(int32_t *)(uintptr_t)ADDR_VIEW_FAR_X)  /* 0x00514E1C */
#define g_viewFarY               (*(int32_t *)(uintptr_t)ADDR_VIEW_FAR_Y)  /* 0x00514E20 */
#define g_viewHold               (*(int32_t *)(uintptr_t)ADDR_VIEW_HOLD)  /* 0x00511E38 */
#define g_viewRect_              ((int32_t *)(uintptr_t)ADDR_VIEW_RECT)  /* 0x004FCF70 */
#define g_viewRectOn             (*(int32_t *)(uintptr_t)ADDR_VIEW_RECT_ON)  /* 0x004FCF58 */
#define g_viewRectPrev           (*(AM2_Rect *)(uintptr_t)ADDR_VIEW_RECT_PREV)  /* 0x00514E24 */
#define g_viewSnap               (*(int32_t *)(uintptr_t)ADDR_VIEW_SNAP)  /* 0x00511E34 */
#define g_viewSpeed              (*(int32_t *)(uintptr_t)ADDR_VIEW_SPEED)  /* 0x004852E0 */
#define g_viewTarget             (*(AM2_Point *)(uintptr_t)ADDR_VIEW_TARGET)  /* 0x00514E08 */
#define g_viewTilesH             (*(int32_t *)(uintptr_t)ADDR_VIEW_TILES_H)  /* 0x00514EA4 */
#define g_viewTilesW             (*(int32_t *)(uintptr_t)ADDR_VIEW_TILES_W)  /* 0x00514EA0 */
#define g_viewX                  (*(int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_X)  /* 0x00514E14 */
#define g_viewY                  (*(int32_t *)(uintptr_t)ADDR_VIEW_ORIGIN_Y)  /* 0x00514E18 */
#define g_visibleTiles           ((uint8_t *)(uintptr_t)ADDR_VISIBLE_TILES)  /* 0x00514EA8 */
#define g_voiceGroups            ((struct VoiceGroup *)(uintptr_t)ADDR_VOICE_GROUPS)  /* 0x00474440 */
#define g_voiceVolume            (*(int32_t *)(uintptr_t)ADDR_VOLUME_VOICE)  /* 0x00512320 */
#define g_volumeAtZero           (*(int32_t *)(uintptr_t)ADDR_VOLUME_AT_ZERO)  /* 0x00512318 */
#define g_watchedTypeId          (*(int32_t *)(uintptr_t)ADDR_WATCHED_TYPE_ID)  /* 0x00516164 */
#define g_waveDir                (*(char **)(uintptr_t)ADDR_WAVE_DIR)  /* 0x004852CC */
#define g_waveNames              ((char **)(uintptr_t)ADDR_WAVE_NAMES)  /* 0x00474360 */
#define g_waveNamesEnd           ((uint8_t *)(uintptr_t)ADDR_WAVE_NAMES_END)  /* 0x00474440 */
#define g_weaponFnSlot0          (*(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT0)  /* 0x005122D4 */
#define g_weaponFnSlot1          (*(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT1)  /* 0x005122D8 */
#define g_weaponFnSlot2          (*(uint32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT2)  /* 0x005122F0 */
#define g_weaponFnSlot3          (*(int32_t *)(uintptr_t)ADDR_WEAPON_FN_SLOT3)  /* 0x005122DC */
#define g_weaponHandlers         ((uint32_t *)(uintptr_t)ADDR_WEAPON_HANDLERS)  /* 0x00489880 */
#define g_weaponOwnerId          (*(uint32_t *)(uintptr_t)ADDR_WEAPON_OWNER_ID)  /* 0x00511E58 */
#define g_weaponPoseFrames       ((int32_t *)(uintptr_t)ADDR_WEAPON_POSE_FRAMES)  /* 0x00474FE0 */
#define g_weaponRangeHi          (*(double *)(uintptr_t)ADDR_WEAPON_RANGE_HI)  /* 0x0046F2E0 */
#define g_weaponRangeK3          (*(double *)(uintptr_t)ADDR_WEAPON_RANGE_K3)  /* 0x0046F2F0 */
#define g_weaponSlot             (*(int32_t *)(uintptr_t)ADDR_WEAPON_SLOT)  /* 0x00511E5C */
#define g_whiteInk               (*(uint8_t *)(uintptr_t)ADDR_COLOUR_WHITE)  /* 0x004FD768 */
#define g_wincpuid               (*(am2_wincpuid_fn *)(uintptr_t)ADDR_WINCPUID_FN)  /* 0x004F9FE0 */
#define g_windowed               (*(int32_t *)(uintptr_t)ADDR_OPT_WINDOWED)  /* 0x00507344 */
#define g_winEnabled             (*(int32_t *)(uintptr_t)ADDR_WIN_ENABLED)  /* 0x00512304 */
#define g_zeroPoint              (*(uint32_t *)(uintptr_t)ADDR_ZERO_POINT)  /* 0x005125A0 */
#define g_zeroXY                 (*(const AM2_Point *)(uintptr_t)ADDR_ZERO_POINT)  /* 0x005125A0, as the point it is */
#define g_zeroRect               ((const AM2_Rect *)(uintptr_t)ADDR_ZERO_RECT)  /* 0x005125A8 */

#endif /* AM2_GLOBALS_H */
