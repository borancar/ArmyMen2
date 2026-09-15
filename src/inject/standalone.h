/* standalone.h -- the function seams, for the drop-in replacement build.
 *
 * src/game reaches a handful of things in the original's .text by address:
 * its statically linked MSVC CRT, its rand, its logger, and the three
 * DirectX creator thunks.  There are 47 and they are the ONLY .text
 * addresses the reconstruction uses for anything but a patch_replace --
 * every other one is a detour target, which a standalone build has nothing
 * to detour.
 *
 * Each is redefined here to the address of a real function, so the call
 * sites -- which cast to their own function-pointer type and call through --
 * do not change.  The injected build includes none of this.
 *
 * TWO ARE NOT LIBC AND MUST NOT BE.  The game's rand is MSVC's LCG, and the
 * sequence is observable in play: reproducing it is what keeps a standalone
 * mission behaving like the original's, so am2_sa_rand implements the
 * multiplier and addend rather than calling the host's rand.  The logger is
 * stubbed to `ret` in this retail build, so it drops its message here too --
 * anything else would put lines on screen the original never printed.
 */
#ifndef AM2_STANDALONE_H
#define AM2_STANDALONE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <direct.h>
#include <io.h>
#include "../platform/crt/crt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MSVC's rand, not the host's: the LCG whose constants are visible in the
 * image at 0x00464420 -- imul 0x343FD, add 0x269EC3, and the answer is bits
 * 16..30 of the seed. */

/* The retail logger is a bare `ret`.  This drops the message for the same
 * reason. */
void am2_sa_log(const char *fmt, ...);

/* The three DirectX creators are the game's own `jmp [IAT]` thunks.  They go
 * through wrappers rather than being named here, so this header -- which
 * every translation unit sees -- does not have to pull in ddraw.h, dinput.h
 * and dsound.h. */
/* __stdcall, NOT cdecl: the game calls these through WINAPI function-pointer
 * typedefs, so a cdecl wrapper leaves the four arguments on the stack that
 * neither side pops. The frame then shifts by sixteen bytes and the CALLER's
 * next parameter read returns a stack address -- which is exactly how this
 * was found, with InitInput's hWnd reading 0x00c3fdbc and SetCooperativeLevel
 * answering E_HANDLE for a window that was demonstrably valid. */
int32_t __stdcall am2_sa_ddraw_create(void *guid, void **out, void *outer);
int32_t __stdcall am2_sa_dinput_create(void *inst, uint32_t ver, void **out,
                                       void *outer);
int32_t __stdcall am2_sa_dsound_create(void *guid, void **out, void *outer);

/* MSVC spells these with a leading underscore and mingw agrees, but the
 * find-file family also shares a STRUCT layout with the caller, so these
 * wrappers are where any divergence gets absorbed. */
intptr_t am2_sa_findfirst(const char *spec, void *data);
int      am2_sa_findnext(intptr_t handle, void *data);
int      am2_sa_findclose(intptr_t handle);

void *am2_sa_operator_new(size_t n);
void  am2_sa_operator_delete(void *p);

/* Seams that live in orig.h itself rather than in src/game, which is why the
 * first sweep for these missed them: it scanned src/game only, and orig_ftell
 * is defined beside the very macros it uses.  The gap surfaced as a fault
 * inside ReadWaveFile, three layers below where it was introduced. */
long am2_sa_ftell(void *fp);

/* 0x0040A6A0 is a one-instruction `jmp 0x0040A660` -- a linker thunk, not a
 * function -- and its target is FreeArmyObjLists, which is reconstructed. */
void am2_sa_free_army_lists(void);

/* ONE address the standalone build has no code for, and it is not a gap: the
 * AM2_PROBE_NOACTION seam exists to call the ORIGINAL action parser, so it
 * cannot mean anything in a build that carries none of the original. It LOGS
 * and returns 0 -- a stub that announces itself beats a jump into unmapped
 * memory, and beats a silent wrong answer by more.
 *
 * The other two were real functions and are gone: 0x00451990 became
 * OnEnterNameOk and 0x004185C0 became HudChatChar. Both were interior
 * addresses of merged entries, which is why remaining.py, coverage.py and
 * checkinstalled all read them as done; the standalone build is the only
 * thing in the tree that cannot call code it does not contain, and it is what
 * found them. */
int32_t am2_sa_unimplemented(void);


/* Tables MSVC placed in .text, which this build does not carry. Extracted
 * into build/standalone/tables.cpp rather than transcribed. */
extern const uint8_t am2_pickup_kind_index[29];

/* Global structures carved out of the carried .rdata/.data blob and
 * transcribed by hand into typed C, so the reconstruction reads them from our
 * own data rather than from the placed image. See the definitions for the
 * address each replaces and why the migration is complete. */
extern const int32_t am2_weapon_pose_frames[104];   /* was 0x00474FE0 */
extern const int32_t am2_pose_by_class[3];           /* was 0x00475180 */
extern const int32_t am2_death_anim_by_code[3];      /* was 0x0047518C */
extern const int32_t am2_hit_pose_by_class[6];       /* was 0x00475198 */
extern const uint8_t am2_formation_slots[72];        /* was 0x00473EA0 */
extern const int16_t am2_air_drop_offsets[6];        /* was 0x00473F74 */
extern const uint8_t am2_air_drop_facings[3];        /* was 0x00473F80 */
extern const int16_t am2_air_frame_hotspots[22];     /* was 0x00473FB0 */
extern const int32_t am2_air_strike_kinds[6];        /* was 0x00473FDC */
extern const uint8_t am2_army_pal_base[4];           /* was 0x00474174 */
/* Air-strike gauge/flight-path scalars (air.cpp). */
extern const int32_t am2_air_gauge_x0, am2_air_gauge_y0, am2_air_gauge_ms;
extern const int32_t am2_air_pass_ms, am2_air_cycle_ms, am2_air_run_ms;
extern const int32_t am2_air_leg1_ms, am2_air_leg2_ms, am2_air_path_in_y;
extern const int32_t am2_air_path_out_y, am2_air_path_mid_y, am2_air_path_apex_x;
extern const int32_t am2_air_path_half_y;
extern const double  am2_air_gauge_slope, am2_air_leg1_slope, am2_air_leg3_slope;
extern const int16_t am2_air_path_turn_x, am2_air_path_away_x;
/* TURN_Y_IN/TURN_Y_OUT are runtime state (AirInitTurnYIn/Out), not migrated. */
/* Roach creature parameters (item.cpp). */
extern const int16_t am2_roach_health, am2_roach_start_frame;
extern const int32_t am2_roach_armour, am2_roach_damage, am2_roach_forvel;
extern const int32_t am2_roach_revvel, am2_roach_foracc, am2_roach_revacc;
extern const int32_t am2_roach_box[4], am2_roach_bite_box[4], am2_roach_row_spec[4];
extern const int32_t am2_field_530_frames;
/* Object hit-boxes and row-specs, int32[4] rects (item.cpp). */
extern const int32_t am2_explosion_box[4], am2_explosion_area_16[4];
extern const int32_t am2_explosion_area_24[4], am2_explosion_area_32[4];
extern const int32_t am2_explosion_row_spec[4], am2_missile_box[4], am2_missile_row_spec[4];
extern const int32_t am2_trooper_box[4], am2_trooper_row_spec[4];
extern const int32_t am2_vehicle_box[4], am2_vehicle_row_spec[4], am2_kind7_box[4];
/* Gameplay scalar parameters (misc.cpp). */
extern const int16_t am2_pillbox_trooper_health, am2_seq_k4_rise;
extern const float   am2_gravity, am2_difficulty_scale;
extern const int32_t am2_view_speed;
/* tick_interval_ms / path_max_nodes are runtime state, not migrated. */
extern const int32_t am2_path_max_searches, am2_path_retry_ms, am2_seq_grid_rows;
extern const int32_t am2_seq_tail_frames, am2_seq_advance_ms, am2_seq_emit_ms;
extern const int32_t am2_seq_k4_step_ms, am2_seq_k4_drift_x, am2_seq_k4_drift_y;
/* Small pointer-free lookup tables (item.cpp; seq_k4_hold in misc.cpp). */
extern const int32_t  am2_trooper_class_value[4], am2_kind_frames[8];
extern const int32_t  am2_vehicle_height_by_kind[6], am2_spiral_dx[4], am2_spiral_dy[4];
extern const uint32_t am2_bit_from_n[4], am2_seq_k4_hold[4];
extern const int16_t  am2_mp_row_coords[32], am2_drop_ring[12];
/* HUD layout (widget.cpp), sprite-grid/counts (sprite.cpp), palette cycle count. */
/* am2_build_menu_rects is not a symbol: its first rect shares storage with
 * am2_pointer_modes[6]'s tail; ADDR_BUILD_MENU_RECTS is numeric (into placed
 * pointer_modes + blob).
 * am2_hud_cmd_spec is runtime-written (HudCmdConstruct) and 280 bytes; not migrated. */
extern const int16_t am2_hud_cmd_offsets[6], am2_hud_sarge_offsets[8];
extern const int16_t am2_hud_squad_slot_xy[24];
extern const int32_t am2_spiral_step[8];
extern const uint32_t am2_map_field_descs[14];
extern const int32_t am2_sprite_grid_rows, am2_sprite_grid_cols, am2_seq_sprite_5_count;
extern const int32_t am2_decal_sprite_count, am2_mark_sprite_count, am2_mp_mark_cols[2];
extern const int32_t am2_palette_cycle_count;
extern const uint32_t am2_palette_cycle_seq[10], am2_palette_cycle_interval;
extern const uint8_t am2_guid_sys_mouse[16], am2_guid_sys_keyboard[16];
extern const uint8_t am2_iid_directdraw2[16], am2_iid_ds3d_listener[16];
extern const uint8_t am2_iid_directplay4a[16], am2_clsid_directplay[16];
extern const uint8_t am2_iid_dplay_lobby3a[16], am2_clsid_dplay_lobby[16];
extern const uint8_t am2_guid_game_property[16], am2_app_guid[16], am2_guid_null[16];
extern const char *const am2_wave_names[56];
extern const char *const am2_cheat_words[41];
extern const char *const am2_script_kind_names[7];   /* script.cpp, was 0x00487C74 */
extern const uint8_t am2_frame_heading_bias[152];    /* maprow.cpp, was 0x004740CC */
extern const uint8_t am2_unit_types[720];            /* place.cpp, was 0x00487898 */
extern const int32_t am2_menu_save_slot[4];          /* surface.cpp, was 0x00476198 */
extern const int32_t am2_shake_presets[44];          /* mapdraw.cpp, was 0x00486170 */
extern const int16_t am2_aim_displace_map[25088];    /* mapdraw.cpp, was 0x00478CDC */
extern const int32_t am2_pad_bit_table[66];          /* pad.cpp, was 0x00486444 */
extern const uint32_t am2_respawn_kind_mask[44];     /* maprow.cpp, was 0x0048C530 */
extern const uint8_t am2_key_defaults[24];           /* gameproc.cpp, was 0x0048AE80 */
extern const int32_t am2_game_version;               /* commmsg.cpp, was 0x00475894 */
/* char* name arrays (verified by dereference). */
extern const char *const am2_item_type_names[44], *const am2_unit_class_names[45];
extern const char *const am2_movie_names[12], *const am2_vehicle_names[6];
extern const char *const am2_sprite_set_dirs[46];
extern const AM2_StateAction am2_state_actions[5];   /* movie.cpp, was 0x0048654C */
/* am2_key_names (AM2_KeyName[95], widget.cpp, was 0x0048AF28) is declared in
 * widget.cpp where AM2_KeyName is defined and used only there; the redirect
 * macros below expand in that TU, so no extern is needed here. */
/* One per-item-kind weapon-handler record: slots 0/1 are handler pointers,
 * 2/3 int params. Defined here so both the definition (widget.cpp) and this
 * declaration share the type. */
struct AM2_WeaponHandler { const void *fn0, *fn1; int32_t p2, p3; };
extern const struct AM2_WeaponHandler am2_weapon_handlers[44]; /* widget.cpp, 0x00489880 */
/* One OPTIONS-dialog declaration record (the reader walks it by byte offset;
 * this struct is only the transcription's, so it lives here). */
struct AM2_Option { int32_t widget, x, y, group, first, last, bit, which;
                    const char *caption; };
extern const struct AM2_Option am2_option_table[43];  /* widget.cpp, 0x004865B8 */
extern const int16_t am2_keyrow_positions[42];        /* widget.cpp, 0x0048AEC8 */
extern const AM2_FontDesc am2_font_descs[3];          /* font.cpp, 0x004897E8 */
/* One cursor-mode record: three handler pointers + seven ints (reader uses
 * AM2_POINTER_MODE_SIZE byte offsets, so this struct is the transcription's). */
struct AM2_PointerMode { const void *pick, *action, *invoke;
                         int32_t a, b, c, d, e, f, g; };
extern const struct AM2_PointerMode am2_pointer_modes[7]; /* widget.cpp, 0x004761B8 */
/* Scalar float/double constants (misc.cpp), read as *(const double/float *). */
extern const double am2_weapon_range_hi, am2_weapon_range_lo, am2_weapon_range_k3;
extern const double am2_sight_range_want, am2_enemy_health_share;
extern const double am2_dbl_zero, am2_dbl_max_period, am2_dbl_ms_per_sec;
extern const double am2_dbl_512, am2_dbl_sin_scale, am2_dbl_one_256, am2_dbl_two_pi;
extern const float  am2_f_one, am2_f_one_hundredth, am2_float_zero;
extern const float  am2_hud_slide_shut, am2_hud_slide_open, am2_roach_reach, am2_ms_to_sec;

#ifdef __cplusplus
}
#endif




#define AM2_SA(fn) ((uintptr_t)(void *)&(fn))

#undef ADDR_CRT_ATEXIT
#undef ADDR_CRT_ATOI
#undef ADDR_CRT_BSEARCH
#undef ADDR_CRT_CHDIR
#undef ADDR_CRT_CHMOD
#undef ADDR_CRT_FFLUSH
#undef ADDR_CRT_FGETS
#undef ADDR_CRT_FINDCLOSE
#undef ADDR_CRT_FINDFIRST
#undef ADDR_CRT_FINDNEXT
#undef ADDR_CRT_FREE
#undef ADDR_CRT_GETCWD
#undef ADDR_CRT_MALLOC
#undef ADDR_CRT_MKDIR
#undef ADDR_CRT_QSORT
#undef ADDR_CRT_REALLOC
#undef ADDR_CRT_REMOVE
#undef ADDR_CRT_RMDIR
#undef ADDR_CRT_STRCHR
#undef ADDR_CRT_STRLWR
#undef ADDR_CRT_STRNCPY
#undef ADDR_CRT_STRNCMP
#undef ADDR_CRT_STRSTR
#undef ADDR_CRT_STRTOD
#undef ADDR_CRT_STRTOK
#undef ADDR_CRT_STRTOL
#undef ADDR_CRT_TIME
#undef ADDR_FCLOSE
#undef ADDR_FOPEN
#undef ADDR_FREAD
#undef ADDR_FSEEK
#undef ADDR_FWRITE
#undef ADDR_GAME_DELETE
#undef ADDR_GAME_MALLOC
#undef ADDR_GAME_OPERATOR_NEW
#undef ADDR_GAME_RAND
#undef ADDR_GAME_SPRINTF
#undef ADDR_GAME_STRICMP
#undef ADDR_LOG
#undef ADDR_REALLOC
#undef ADDR_MEMMOVE
#undef ADDR_VSPRINTF
#undef ADDR_CRT_ENDSTDIO
#undef ADDR_CRT_SEH_RESTORE
#undef ADDR_CRT_ONEXITINIT
#undef ADDR_CRT_INITSTDIO
#undef ADDR_CRT_INITMBCTABLE
#undef ADDR_CRT_SEH_SET
#undef ADDR_CRT_FPMATH
#undef ADDR_CRT_EXIT_QUICK
#undef ADDR_DIRECTDRAWCREATE
#undef ADDR_DIRECTINPUTCREATE
#undef ADDR_DIRECTSOUNDCREATE

#define ADDR_CRT_ATEXIT       AM2_SA(crt_atexit)
#define ADDR_CRT_ATOI         AM2_SA(crt_atoi)
#define ADDR_CRT_BSEARCH      AM2_SA(crt_bsearch)
#define ADDR_CRT_CHDIR        AM2_SA(crt_chdir)
#define ADDR_CRT_CHMOD        AM2_SA(crt_chmod)
#define ADDR_CRT_FFLUSH       AM2_SA(crt_fflush)
#define ADDR_CRT_FGETS        AM2_SA(crt_fgets)
#define ADDR_CRT_FINDCLOSE    AM2_SA(crt_findclose)
#define ADDR_CRT_FINDFIRST    AM2_SA(crt_findfirst)
#define ADDR_CRT_FINDNEXT     AM2_SA(crt_findnext)
#define ADDR_CRT_FREE         AM2_SA(crt_free)
#define ADDR_CRT_GETCWD       AM2_SA(crt_getcwd)
#define ADDR_CRT_MALLOC       AM2_SA(crt_malloc)
#define ADDR_CRT_MKDIR        AM2_SA(crt_mkdir)
#define ADDR_CRT_QSORT        AM2_SA(crt_qsort)
#define ADDR_CRT_REALLOC      AM2_SA(crt_realloc)
#define ADDR_CRT_REMOVE       AM2_SA(crt_remove)
#define ADDR_CRT_RMDIR        AM2_SA(crt_rmdir)
#define ADDR_CRT_STRCHR       AM2_SA(crt_strchr)
#define ADDR_CRT_STRLWR       AM2_SA(crt_strlwr)
#define ADDR_CRT_STRNCPY      AM2_SA(crt_strncpy)
#define ADDR_CRT_STRNCMP     AM2_SA(crt_strncmp)
#define ADDR_CRT_STRSTR       AM2_SA(crt_strstr)
#define ADDR_CRT_STRTOD       AM2_SA(crt_strtod)
#define ADDR_CRT_STRTOK       AM2_SA(crt_strtok)
#define ADDR_CRT_STRTOL       AM2_SA(crt_strtol)
#define ADDR_CRT_TIME         AM2_SA(crt_time)
#define ADDR_FCLOSE           AM2_SA(crt_fclose)
/* The CRT's own stdio, over CreateFileA and friends: the native build's
 * kernel32crt.cpp translates the game's Windows paths there -- separators,
 * a drive letter, case -- and Wine's kernel32 does the same for the
 * standalone, so one fopen serves both. */
#define ADDR_FOPEN            AM2_SA(crt_fopen)
#define ADDR_FREAD            AM2_SA(crt_fread)
#define ADDR_FSEEK            AM2_SA(crt_fseek)
#define ADDR_FWRITE           AM2_SA(crt_fwrite)
#define ADDR_GAME_DELETE      AM2_SA(am2_sa_operator_delete)
#define ADDR_GAME_MALLOC      AM2_SA(crt_malloc)
#define ADDR_GAME_OPERATOR_NEW AM2_SA(am2_sa_operator_new)
#define ADDR_GAME_RAND        AM2_SA(crt_rand)
#define ADDR_GAME_SPRINTF     AM2_SA(crt_sprintf)
#define ADDR_GAME_STRICMP     AM2_SA(crt_stricmp)
#define ADDR_LOG              AM2_SA(am2_sa_log)
#define ADDR_REALLOC          AM2_SA(crt_realloc)
#define ADDR_MEMMOVE          AM2_SA(crt_memmove)
#define ADDR_VSPRINTF         AM2_SA(crt_vsprintf)
/* The two entries the image's terminator tables hold, which mkglobals
 * rewrites in the carried .rdata to these: exit.cpp's doexit walks the
 * tables and finds the reconstructions there. */
#define ADDR_CRT_ENDSTDIO     AM2_SA(crt_endstdio)
#define ADDR_CRT_SEH_RESTORE  AM2_SA(crt_seh_restore)
/* And the initializer table's four entries, the pointer _cinit calls
 * _fpmath through, and the one _amsg_exit calls _exit through. */
#define ADDR_CRT_ONEXITINIT   AM2_SA(crt_onexitinit)
#define ADDR_CRT_INITSTDIO    AM2_SA(crt_initstdio)
#define ADDR_CRT_INITMBCTABLE AM2_SA(crt_initmbctable)
#define ADDR_CRT_SEH_SET      AM2_SA(crt_seh_set)
#define ADDR_CRT_FPMATH       AM2_SA(crt_fpmath)
#define ADDR_CRT_EXIT_QUICK   AM2_SA(crt_exit_quick)
#define ADDR_DIRECTDRAWCREATE  AM2_SA(am2_sa_ddraw_create)
#define ADDR_DIRECTINPUTCREATE AM2_SA(am2_sa_dinput_create)
#define ADDR_DIRECTSOUNDCREATE AM2_SA(am2_sa_dsound_create)

#undef ADDR_FTELL
#undef ADDR_GAME_FREE
#undef ADDR_FREE_ARMY_LISTS_ALIAS
#undef ADDR_SCRIPT_PARSE_ACTION
#define ADDR_FTELL            AM2_SA(crt_ftell)
#define ADDR_GAME_FREE        AM2_SA(crt_free)
#define ADDR_FREE_ARMY_LISTS_ALIAS AM2_SA(am2_sa_free_army_lists)

/* The DEVELOPMENT binary (make native-dev, AM2_DEVTOOLS) adds savestates
 * and the per-frame heap scan; src/standalone/devtools.cpp. Its heap is the
 * same as the player's -- the CRT's, over the platform's fixed-address
 * arena (src/platform/fixedheap.cpp) -- so nothing here is redirected. */
#ifdef AM2_DEVTOOLS
#ifdef __cplusplus
extern "C" {
#endif
void  devtools_init(void);
#ifdef __cplusplus
}
#endif
#endif
#define ADDR_SCRIPT_PARSE_ACTION AM2_SA(am2_sa_unimplemented)


/* AM2_ITEM_KIND_IS_SPECIAL reads a 29-byte table at 0x00433770, inside the
 * .text range a standalone build does not carry -- so every byte read 0xCC
 * and the predicate was false for every kind, quietly dropping kinds 1, 7,
 * 8, 9, 10 and 29 out of the special set. */
#undef AM2_ITEM_KIND_IS_SPECIAL
#define AM2_ITEM_KIND_IS_SPECIAL(kind) \
    ((uint32_t)((kind) - 1) <= 0x1Cu \
     && am2_pickup_kind_index[(kind) - 1] == 0)

/* The transcribed global structures replace the image's copy at their own
 * addresses. am2_image_slide is 0 in every placed build (image.cpp), so the
 * call sites' AM2_IMAGE() wrapper leaves these pointers untouched. */
#undef ADDR_WEAPON_POSE_FRAMES
#define ADDR_WEAPON_POSE_FRAMES ((uintptr_t)(const void *)am2_weapon_pose_frames)
#undef ADDR_POSE_BY_CLASS
#define ADDR_POSE_BY_CLASS      ((uintptr_t)(const void *)am2_pose_by_class)
#undef ADDR_DEATH_ANIM_BY_CODE
#define ADDR_DEATH_ANIM_BY_CODE ((uintptr_t)(const void *)am2_death_anim_by_code)
#undef ADDR_HIT_POSE_BY_CLASS
#define ADDR_HIT_POSE_BY_CLASS  ((uintptr_t)(const void *)am2_hit_pose_by_class)

/* AI_MOVE_STATE and its ALT are not separate storage: they OVERLAP
 * am2_weapon_pose_frames. 0x004750B4 is that array's index 53 (0xD4/4) and
 * 0x004750C0 is index 56, and the six entries there read {2,8,9,3,8,9} --
 * exactly the two int32[3] tables orig.h documents. So they alias into the
 * transcribed array rather than duplicating it. */
#undef ADDR_AI_MOVE_STATE
#define ADDR_AI_MOVE_STATE      ((uintptr_t)(const void *)&am2_weapon_pose_frames[53])
#undef ADDR_AI_MOVE_STATE_ALT
#define ADDR_AI_MOVE_STATE_ALT  ((uintptr_t)(const void *)&am2_weapon_pose_frames[56])

#undef ADDR_FORMATION_SLOTS
#define ADDR_FORMATION_SLOTS    ((uintptr_t)(const void *)am2_formation_slots)
#undef ADDR_AIR_DROP_OFFSETS
#define ADDR_AIR_DROP_OFFSETS   ((uintptr_t)(const void *)am2_air_drop_offsets)
#undef ADDR_AIR_DROP_FACINGS
#define ADDR_AIR_DROP_FACINGS   ((uintptr_t)(const void *)am2_air_drop_facings)
#undef ADDR_AIR_FRAME_HOTSPOTS
#define ADDR_AIR_FRAME_HOTSPOTS ((uintptr_t)(const void *)am2_air_frame_hotspots)
#undef ADDR_AIR_STRIKE_KINDS
#define ADDR_AIR_STRIKE_KINDS   ((uintptr_t)(const void *)am2_air_strike_kinds)
#undef ADDR_ARMY_PAL_BASE
#define ADDR_ARMY_PAL_BASE      ((uintptr_t)(const void *)am2_army_pal_base)

#undef ADDR_AIR_GAUGE_X0
#define ADDR_AIR_GAUGE_X0       ((uintptr_t)(const void *)&am2_air_gauge_x0)
#undef ADDR_AIR_GAUGE_Y0
#define ADDR_AIR_GAUGE_Y0       ((uintptr_t)(const void *)&am2_air_gauge_y0)
#undef ADDR_AIR_GAUGE_SLOPE
#define ADDR_AIR_GAUGE_SLOPE    ((uintptr_t)(const void *)&am2_air_gauge_slope)
/* ADDR_AIR_STRIKE_SLOPE is the same 0x00473F28 double (0.43): the gauge slope
 * doubles as the strike slope. One constant, two names -- same C symbol. */
#undef ADDR_AIR_STRIKE_SLOPE
#define ADDR_AIR_STRIKE_SLOPE   ((uintptr_t)(const void *)&am2_air_gauge_slope)
#undef ADDR_AIR_GAUGE_MS
#define ADDR_AIR_GAUGE_MS       ((uintptr_t)(const void *)&am2_air_gauge_ms)
#undef ADDR_AIR_PASS_MS
#define ADDR_AIR_PASS_MS        ((uintptr_t)(const void *)&am2_air_pass_ms)
#undef ADDR_AIR_CYCLE_MS
#define ADDR_AIR_CYCLE_MS       ((uintptr_t)(const void *)&am2_air_cycle_ms)
#undef ADDR_AIR_RUN_MS
#define ADDR_AIR_RUN_MS         ((uintptr_t)(const void *)&am2_air_run_ms)
#undef ADDR_AIR_LEG1_MS
#define ADDR_AIR_LEG1_MS        ((uintptr_t)(const void *)&am2_air_leg1_ms)
#undef ADDR_AIR_LEG2_MS
#define ADDR_AIR_LEG2_MS        ((uintptr_t)(const void *)&am2_air_leg2_ms)
#undef ADDR_AIR_PATH_IN_Y
#define ADDR_AIR_PATH_IN_Y      ((uintptr_t)(const void *)&am2_air_path_in_y)
#undef ADDR_AIR_PATH_OUT_Y
#define ADDR_AIR_PATH_OUT_Y     ((uintptr_t)(const void *)&am2_air_path_out_y)
#undef ADDR_AIR_LEG1_SLOPE
#define ADDR_AIR_LEG1_SLOPE     ((uintptr_t)(const void *)&am2_air_leg1_slope)
#undef ADDR_AIR_LEG3_SLOPE
#define ADDR_AIR_LEG3_SLOPE     ((uintptr_t)(const void *)&am2_air_leg3_slope)
#undef ADDR_AIR_PATH_MID_Y
#define ADDR_AIR_PATH_MID_Y     ((uintptr_t)(const void *)&am2_air_path_mid_y)
#undef ADDR_AIR_PATH_APEX_X
#define ADDR_AIR_PATH_APEX_X    ((uintptr_t)(const void *)&am2_air_path_apex_x)
#undef ADDR_AIR_PATH_HALF_Y
#define ADDR_AIR_PATH_HALF_Y    ((uintptr_t)(const void *)&am2_air_path_half_y)
#undef ADDR_AIR_PATH_TURN_X
#define ADDR_AIR_PATH_TURN_X    ((uintptr_t)(const void *)&am2_air_path_turn_x)
#undef ADDR_AIR_PATH_AWAY_X
#define ADDR_AIR_PATH_AWAY_X    ((uintptr_t)(const void *)&am2_air_path_away_x)
/* ADDR_AIR_PATH_TURN_Y_IN / _OUT are runtime state written by AirInitTurnYIn/Out;
 * left at their writable .origdat placement rather than redirected to a const. */

#undef ADDR_ROACH_HEALTH
#define ADDR_ROACH_HEALTH       ((uintptr_t)(const void *)&am2_roach_health)
#undef ADDR_ROACH_ARMOUR
#define ADDR_ROACH_ARMOUR       ((uintptr_t)(const void *)&am2_roach_armour)
#undef ADDR_ROACH_DAMAGE
#define ADDR_ROACH_DAMAGE       ((uintptr_t)(const void *)&am2_roach_damage)
#undef ADDR_ROACH_FORVEL
#define ADDR_ROACH_FORVEL       ((uintptr_t)(const void *)&am2_roach_forvel)
#undef ADDR_ROACH_REVVEL
#define ADDR_ROACH_REVVEL       ((uintptr_t)(const void *)&am2_roach_revvel)
#undef ADDR_ROACH_FORACC
#define ADDR_ROACH_FORACC       ((uintptr_t)(const void *)&am2_roach_foracc)
#undef ADDR_ROACH_REVACC
#define ADDR_ROACH_REVACC       ((uintptr_t)(const void *)&am2_roach_revacc)
#undef ADDR_ROACH_BOX
#define ADDR_ROACH_BOX          ((uintptr_t)(const void *)am2_roach_box)
#undef ADDR_ROACH_BITE_BOX
#define ADDR_ROACH_BITE_BOX     ((uintptr_t)(const void *)am2_roach_bite_box)
#undef ADDR_ROACH_ROW_SPEC
#define ADDR_ROACH_ROW_SPEC     ((uintptr_t)(const void *)am2_roach_row_spec)
#undef ADDR_FIELD_530_FRAMES
#define ADDR_FIELD_530_FRAMES   ((uintptr_t)(const void *)&am2_field_530_frames)
#undef ADDR_ROACH_START_FRAME
#define ADDR_ROACH_START_FRAME  ((uintptr_t)(const void *)&am2_roach_start_frame)

#undef ADDR_EXPLOSION_BOX
#define ADDR_EXPLOSION_BOX      ((uintptr_t)(const void *)am2_explosion_box)
#undef ADDR_EXPLOSION_AREA_16
#define ADDR_EXPLOSION_AREA_16  ((uintptr_t)(const void *)am2_explosion_area_16)
#undef ADDR_EXPLOSION_AREA_24
#define ADDR_EXPLOSION_AREA_24  ((uintptr_t)(const void *)am2_explosion_area_24)
#undef ADDR_EXPLOSION_AREA_32
#define ADDR_EXPLOSION_AREA_32  ((uintptr_t)(const void *)am2_explosion_area_32)
#undef ADDR_EXPLOSION_ROW_SPEC
#define ADDR_EXPLOSION_ROW_SPEC ((uintptr_t)(const void *)am2_explosion_row_spec)
#undef ADDR_MISSILE_BOX
#define ADDR_MISSILE_BOX        ((uintptr_t)(const void *)am2_missile_box)
#undef ADDR_MISSILE_ROW_SPEC
#define ADDR_MISSILE_ROW_SPEC   ((uintptr_t)(const void *)am2_missile_row_spec)
#undef ADDR_TROOPER_BOX
#define ADDR_TROOPER_BOX        ((uintptr_t)(const void *)am2_trooper_box)
#undef ADDR_TROOPER_ROW_SPEC
#define ADDR_TROOPER_ROW_SPEC   ((uintptr_t)(const void *)am2_trooper_row_spec)
#undef ADDR_VEHICLE_BOX
#define ADDR_VEHICLE_BOX        ((uintptr_t)(const void *)am2_vehicle_box)
#undef ADDR_VEHICLE_ROW_SPEC
#define ADDR_VEHICLE_ROW_SPEC   ((uintptr_t)(const void *)am2_vehicle_row_spec)
#undef ADDR_KIND7_BOX
#define ADDR_KIND7_BOX          ((uintptr_t)(const void *)am2_kind7_box)

#undef ADDR_PILLBOX_TROOPER_HEALTH
#define ADDR_PILLBOX_TROOPER_HEALTH ((uintptr_t)(const void *)&am2_pillbox_trooper_health)
#undef ADDR_GRAVITY
#define ADDR_GRAVITY            ((uintptr_t)(const void *)&am2_gravity)
#undef ADDR_VIEW_SPEED
#define ADDR_VIEW_SPEED         ((uintptr_t)(const void *)&am2_view_speed)
/* ADDR_TICK_INTERVAL_MS is runtime state (ResetLevelState); not redirected. */
#undef ADDR_DIFFICULTY_SCALE
#define ADDR_DIFFICULTY_SCALE   ((uintptr_t)(const void *)&am2_difficulty_scale)
/* ADDR_PATH_MAX_NODES adapts at runtime (RegionBudget); not redirected. */
#undef ADDR_PATH_MAX_SEARCHES
#define ADDR_PATH_MAX_SEARCHES  ((uintptr_t)(const void *)&am2_path_max_searches)
#undef ADDR_PATH_RETRY_MS
#define ADDR_PATH_RETRY_MS      ((uintptr_t)(const void *)&am2_path_retry_ms)
#undef ADDR_SEQ_GRID_ROWS
#define ADDR_SEQ_GRID_ROWS      ((uintptr_t)(const void *)&am2_seq_grid_rows)
#undef ADDR_SEQ_TAIL_FRAMES
#define ADDR_SEQ_TAIL_FRAMES    ((uintptr_t)(const void *)&am2_seq_tail_frames)
#undef ADDR_SEQ_ADVANCE_MS
#define ADDR_SEQ_ADVANCE_MS     ((uintptr_t)(const void *)&am2_seq_advance_ms)
#undef ADDR_SEQ_EMIT_MS
#define ADDR_SEQ_EMIT_MS        ((uintptr_t)(const void *)&am2_seq_emit_ms)
#undef ADDR_SEQ_K4_STEP_MS
#define ADDR_SEQ_K4_STEP_MS     ((uintptr_t)(const void *)&am2_seq_k4_step_ms)
#undef ADDR_SEQ_K4_RISE
#define ADDR_SEQ_K4_RISE        ((uintptr_t)(const void *)&am2_seq_k4_rise)
#undef ADDR_SEQ_K4_DRIFT_X
#define ADDR_SEQ_K4_DRIFT_X     ((uintptr_t)(const void *)&am2_seq_k4_drift_x)
#undef ADDR_SEQ_K4_DRIFT_Y
#define ADDR_SEQ_K4_DRIFT_Y     ((uintptr_t)(const void *)&am2_seq_k4_drift_y)

#undef ADDR_TROOPER_CLASS_VALUE
#define ADDR_TROOPER_CLASS_VALUE ((uintptr_t)(const void *)am2_trooper_class_value)
#undef ADDR_BIT_FROM_N
#define ADDR_BIT_FROM_N         ((uintptr_t)(const void *)am2_bit_from_n)
#undef ADDR_KIND_FRAMES
#define ADDR_KIND_FRAMES        ((uintptr_t)(const void *)am2_kind_frames)
#undef ADDR_VEHICLE_HEIGHT_BY_KIND
#define ADDR_VEHICLE_HEIGHT_BY_KIND ((uintptr_t)(const void *)am2_vehicle_height_by_kind)
#undef ADDR_MP_ROW_COORDS
#define ADDR_MP_ROW_COORDS      ((uintptr_t)(const void *)am2_mp_row_coords)
#undef ADDR_DROP_RING
#define ADDR_DROP_RING          ((uintptr_t)(const void *)am2_drop_ring)
#undef ADDR_SPIRAL_DX
#define ADDR_SPIRAL_DX          ((uintptr_t)(const void *)am2_spiral_dx)
#undef ADDR_SPIRAL_DY
#define ADDR_SPIRAL_DY          ((uintptr_t)(const void *)am2_spiral_dy)
#undef ADDR_SEQ_K4_HOLD
#define ADDR_SEQ_K4_HOLD        ((uintptr_t)(const void *)am2_seq_k4_hold)

/* ADDR_HUD_CMD_SPEC is runtime-written and 280 bytes; not redirected (blob).
 * ADDR_BUILD_MENU_RECTS is numeric too: rect 0 shares am2_pointer_modes[6]'s
 * placed tail, rect 1+ is blob. */
#undef ADDR_HUD_SQUAD_SLOT_XY
#define ADDR_HUD_SQUAD_SLOT_XY  ((uintptr_t)(const void *)am2_hud_squad_slot_xy)
#undef ADDR_SPIRAL_STEP
#define ADDR_SPIRAL_STEP        ((uintptr_t)(const void *)am2_spiral_step)
#undef ADDR_MAP_FIELD_DESCS
#define ADDR_MAP_FIELD_DESCS    ((uintptr_t)(const void *)am2_map_field_descs)
#undef ADDR_HUD_CMD_OFFSETS
#define ADDR_HUD_CMD_OFFSETS    ((uintptr_t)(const void *)am2_hud_cmd_offsets)
#undef ADDR_HUD_SARGE_OFFSETS
#define ADDR_HUD_SARGE_OFFSETS  ((uintptr_t)(const void *)am2_hud_sarge_offsets)
#undef ADDR_SPRITE_GRID_ROWS
#define ADDR_SPRITE_GRID_ROWS   ((uintptr_t)(const void *)&am2_sprite_grid_rows)
#undef ADDR_SPRITE_GRID_COLS
#define ADDR_SPRITE_GRID_COLS   ((uintptr_t)(const void *)&am2_sprite_grid_cols)
#undef ADDR_SEQ_SPRITE_5_COUNT
#define ADDR_SEQ_SPRITE_5_COUNT ((uintptr_t)(const void *)&am2_seq_sprite_5_count)
#undef ADDR_DECAL_SPRITE_COUNT
#define ADDR_DECAL_SPRITE_COUNT ((uintptr_t)(const void *)&am2_decal_sprite_count)
#undef ADDR_MARK_SPRITE_COUNT
#define ADDR_MARK_SPRITE_COUNT  ((uintptr_t)(const void *)&am2_mark_sprite_count)
#undef ADDR_MP_MARK_COLS
#define ADDR_MP_MARK_COLS       ((uintptr_t)(const void *)am2_mp_mark_cols)
#undef ADDR_PALETTE_CYCLE_COUNT
#define ADDR_PALETTE_CYCLE_COUNT ((uintptr_t)(const void *)&am2_palette_cycle_count)
#undef ADDR_PALETTE_CYCLE_SEQ
#define ADDR_PALETTE_CYCLE_SEQ  ((uintptr_t)(const void *)am2_palette_cycle_seq)
#undef ADDR_PALETTE_CYCLE_INTERVAL
#define ADDR_PALETTE_CYCLE_INTERVAL ((uintptr_t)(const void *)&am2_palette_cycle_interval)
#undef ADDR_GUID_SYS_MOUSE
#define ADDR_GUID_SYS_MOUSE     ((uintptr_t)(const void *)am2_guid_sys_mouse)
#undef ADDR_GUID_SYS_KEYBOARD
#define ADDR_GUID_SYS_KEYBOARD  ((uintptr_t)(const void *)am2_guid_sys_keyboard)
#undef ADDR_IID_DIRECTDRAW2
#define ADDR_IID_DIRECTDRAW2    ((uintptr_t)(const void *)am2_iid_directdraw2)
#undef ADDR_IID_DS3D_LISTENER
#define ADDR_IID_DS3D_LISTENER  ((uintptr_t)(const void *)am2_iid_ds3d_listener)
#undef ADDR_IID_DIRECTPLAY4A
#define ADDR_IID_DIRECTPLAY4A   ((uintptr_t)(const void *)am2_iid_directplay4a)
#undef ADDR_CLSID_DIRECTPLAY
#define ADDR_CLSID_DIRECTPLAY   ((uintptr_t)(const void *)am2_clsid_directplay)
#undef ADDR_IID_DPLAY_LOBBY3A
#define ADDR_IID_DPLAY_LOBBY3A  ((uintptr_t)(const void *)am2_iid_dplay_lobby3a)
#undef ADDR_CLSID_DPLAY_LOBBY
#define ADDR_CLSID_DPLAY_LOBBY  ((uintptr_t)(const void *)am2_clsid_dplay_lobby)
#undef ADDR_GUID_GAME_PROPERTY
#define ADDR_GUID_GAME_PROPERTY ((uintptr_t)(const void *)am2_guid_game_property)
#undef ADDR_APP_GUID
#define ADDR_APP_GUID           ((uintptr_t)(const void *)am2_app_guid)
#undef ADDR_GUID_NULL
#define ADDR_GUID_NULL          ((uintptr_t)(const void *)am2_guid_null)
#undef ADDR_WAVE_NAMES
#define ADDR_WAVE_NAMES         ((uintptr_t)(const void *)am2_wave_names)
#undef ADDR_WAVE_NAMES_END
#define ADDR_WAVE_NAMES_END     ((uintptr_t)(const void *)(am2_wave_names + 56))
/* am2_voice_groups (audio.cpp, AM2_VoiceGroup[30], was 0x00474440) is used
 * only there; the redirect expands in that TU. It sits right after
 * am2_wave_names, so it and ADDR_WAVE_NAMES_END resolve to the same address. */
#undef ADDR_VOICE_GROUPS
#define ADDR_VOICE_GROUPS       ((uintptr_t)(const void *)am2_voice_groups)
#undef ADDR_SCRIPT_KIND_NAMES
#define ADDR_SCRIPT_KIND_NAMES  ((uintptr_t)(const void *)am2_script_kind_names)
/* am2_soldier_names (AM2_SoldierName[62], item.cpp, was 0x00489BF8) is used and
 * declared only in item.cpp; this redirect expands there. Non-const because
 * TakeSoldierName writes the `taken` field. */
#undef ADDR_SOLDIER_NAMES
#define ADDR_SOLDIER_NAMES      ((uintptr_t)(const void *)am2_soldier_names)
#undef ADDR_KEY_NAME_TABLE
#define ADDR_KEY_NAME_TABLE     ((uintptr_t)(const void *)am2_key_names)
#undef ADDR_KEY_NAME_TABLE_END
#define ADDR_KEY_NAME_TABLE_END ((uintptr_t)(const void *)(am2_key_names + 95))
#undef ADDR_CHEAT_WORDS
#define ADDR_CHEAT_WORDS        ((uintptr_t)(const void *)am2_cheat_words)
/* am2_def_keywords (AM2_DefKeyword[101], definfo.cpp, was 0x00476FE0) is
 * declared and used only in definfo.cpp, where this macro expands. */
#undef ADDR_DEF_NAME_TABLE
#define ADDR_DEF_NAME_TABLE     ((uintptr_t)(const void *)am2_def_keywords)
#undef ADDR_FRAME_HEADING_BIAS
#define ADDR_FRAME_HEADING_BIAS ((uintptr_t)(const void *)am2_frame_heading_bias)
#undef ADDR_MENU_SAVE_SLOT
#define ADDR_MENU_SAVE_SLOT     ((uintptr_t)(const void *)am2_menu_save_slot)
#undef ADDR_UNIT_TYPES
#define ADDR_UNIT_TYPES         ((uintptr_t)(const void *)am2_unit_types)
#undef ADDR_SHAKE_PRESETS
#define ADDR_SHAKE_PRESETS      ((uintptr_t)(const void *)am2_shake_presets)
#undef ADDR_AIM_DISPLACE_MAP
#define ADDR_AIM_DISPLACE_MAP   ((uintptr_t)(const void *)am2_aim_displace_map)
#undef ADDR_PAD_BIT_TABLE
#define ADDR_PAD_BIT_TABLE      ((uintptr_t)(const void *)am2_pad_bit_table)
#undef ADDR_RESPAWN_KIND_MASK
#define ADDR_RESPAWN_KIND_MASK  ((uintptr_t)(const void *)am2_respawn_kind_mask)
#undef ADDR_KEY_DEFAULTS
#define ADDR_KEY_DEFAULTS       ((uintptr_t)(const void *)am2_key_defaults)
#undef ADDR_GAME_VERSION
#define ADDR_GAME_VERSION       ((uintptr_t)(const void *)&am2_game_version)
#undef ADDR_ITEM_TYPE_NAMES
#define ADDR_ITEM_TYPE_NAMES    ((uintptr_t)(const void *)am2_item_type_names)
#undef ADDR_UNIT_CLASS_NAMES
#define ADDR_UNIT_CLASS_NAMES   ((uintptr_t)(const void *)am2_unit_class_names)
#undef ADDR_MOVIE_NAMES
#define ADDR_MOVIE_NAMES        ((uintptr_t)(const void *)am2_movie_names)
#undef ADDR_VEHICLE_NAMES
#define ADDR_VEHICLE_NAMES      ((uintptr_t)(const void *)am2_vehicle_names)
#undef ADDR_SPRITE_SET_DIRS
#define ADDR_SPRITE_SET_DIRS    ((uintptr_t)(const void *)am2_sprite_set_dirs)
#undef ADDR_STATE_ACTIONS
#define ADDR_STATE_ACTIONS      ((uintptr_t)(const void *)am2_state_actions)
#undef ADDR_WEAPON_HANDLERS
#define ADDR_WEAPON_HANDLERS    ((uintptr_t)(const void *)am2_weapon_handlers)
#undef ADDR_OPTION_TABLE
#define ADDR_OPTION_TABLE       ((uintptr_t)(const void *)am2_option_table)
/* The reader loops rec < OPTION_TABLE_END, so the bound must be one past OUR
 * array, not the blob's. */
#undef ADDR_OPTION_TABLE_END
#define ADDR_OPTION_TABLE_END   ((uintptr_t)(const void *)&am2_option_table[43])
#undef ADDR_KEYROW_POSITIONS
#define ADDR_KEYROW_POSITIONS   ((uintptr_t)(const void *)am2_keyrow_positions)
#undef ADDR_FONT_DESCS
#define ADDR_FONT_DESCS         ((uintptr_t)(const void *)am2_font_descs)
#undef ADDR_POINTER_MODES
#define ADDR_POINTER_MODES      ((uintptr_t)(const void *)am2_pointer_modes)

#undef ADDR_WEAPON_RANGE_HI
#define ADDR_WEAPON_RANGE_HI    ((uintptr_t)(const void *)&am2_weapon_range_hi)
#undef ADDR_WEAPON_RANGE_LO
#define ADDR_WEAPON_RANGE_LO    ((uintptr_t)(const void *)&am2_weapon_range_lo)
#undef ADDR_WEAPON_RANGE_K3
#define ADDR_WEAPON_RANGE_K3    ((uintptr_t)(const void *)&am2_weapon_range_k3)
#undef ADDR_SIGHT_RANGE_WANT
#define ADDR_SIGHT_RANGE_WANT   ((uintptr_t)(const void *)&am2_sight_range_want)
#undef ADDR_ENEMY_HEALTH_SHARE
#define ADDR_ENEMY_HEALTH_SHARE ((uintptr_t)(const void *)&am2_enemy_health_share)
#undef ADDR_DBL_ZERO
#define ADDR_DBL_ZERO           ((uintptr_t)(const void *)&am2_dbl_zero)
#undef ADDR_DBL_MAX_PERIOD
#define ADDR_DBL_MAX_PERIOD     ((uintptr_t)(const void *)&am2_dbl_max_period)
#undef ADDR_DBL_MS_PER_SEC
#define ADDR_DBL_MS_PER_SEC     ((uintptr_t)(const void *)&am2_dbl_ms_per_sec)
#undef ADDR_DBL_512
#define ADDR_DBL_512            ((uintptr_t)(const void *)&am2_dbl_512)
#undef ADDR_DBL_SIN_SCALE
#define ADDR_DBL_SIN_SCALE      ((uintptr_t)(const void *)&am2_dbl_sin_scale)
#undef ADDR_DBL_ONE_256
#define ADDR_DBL_ONE_256        ((uintptr_t)(const void *)&am2_dbl_one_256)
#undef ADDR_DBL_TWO_PI
#define ADDR_DBL_TWO_PI         ((uintptr_t)(const void *)&am2_dbl_two_pi)
#undef ADDR_F_ONE
#define ADDR_F_ONE              ((uintptr_t)(const void *)&am2_f_one)
#undef ADDR_F_ONE_HUNDREDTH
#define ADDR_F_ONE_HUNDREDTH    ((uintptr_t)(const void *)&am2_f_one_hundredth)
#undef ADDR_FLOAT_ZERO
#define ADDR_FLOAT_ZERO         ((uintptr_t)(const void *)&am2_float_zero)
#undef ADDR_HUD_SLIDE_SHUT
#define ADDR_HUD_SLIDE_SHUT     ((uintptr_t)(const void *)&am2_hud_slide_shut)
#undef ADDR_HUD_SLIDE_OPEN
#define ADDR_HUD_SLIDE_OPEN     ((uintptr_t)(const void *)&am2_hud_slide_open)
#undef ADDR_ROACH_REACH
#define ADDR_ROACH_REACH        ((uintptr_t)(const void *)&am2_roach_reach)
#undef ADDR_MS_TO_SEC
#define ADDR_MS_TO_SEC          ((uintptr_t)(const void *)&am2_ms_to_sec)

/* CRT cumulative day-of-year tables (time.cpp, pure const int32 data). */
#undef ADDR_CRT_LPDAYS
#define ADDR_CRT_LPDAYS         ((uintptr_t)(const void *)am2_crt_lpdays)
#undef ADDR_CRT_DAYS
#define ADDR_CRT_DAYS           ((uintptr_t)(const void *)am2_crt_days)

/* CRT OS-error -> errno map (lowio.cpp) and multibyte-codepage init tables
 * (startup.cpp), pure const data. ERRTABLE_END is the scan bound, one past. */
#undef ADDR_CRT_ERRTABLE
#define ADDR_CRT_ERRTABLE       ((uintptr_t)(const void *)am2_crt_errtable)
#undef ADDR_CRT_ERRTABLE_END
#define ADDR_CRT_ERRTABLE_END   ((uintptr_t)(const void *)&am2_crt_errtable[90])
#undef ADDR_CRT_MBCTYPE_RANGE_FLAGS
#define ADDR_CRT_MBCTYPE_RANGE_FLAGS ((uintptr_t)(const void *)am2_crt_mbctype_range_flags)
#undef ADDR_CRT_MBCP_TABLE
#define ADDR_CRT_MBCP_TABLE     ((uintptr_t)(const void *)am2_crt_mbcp_table)

/* CRT float-conversion parameter blocks (strtod.cpp), pure const int data. */
#undef ADDR_CRT_CVTINFO_DOUBLE
#define ADDR_CRT_CVTINFO_DOUBLE ((uintptr_t)(const void *)am2_crt_cvtinfo_double)

/* CRT runtime-error message table (startup.cpp), mixed {int, char*}. */
#undef ADDR_CRT_RTERR_TABLE
#define ADDR_CRT_RTERR_TABLE    ((uintptr_t)(const void *)am2_crt_rterr_table)

/* CRT base-ten scaling tables (fltcvt.cpp), pure const long-double data. */
#undef ADDR_CRT_POW10_TABLE
#define ADDR_CRT_POW10_TABLE    ((uintptr_t)(const void *)am2_crt_pow10_table)
#undef ADDR_CRT_POW10_NEG_TABLE
#define ADDR_CRT_POW10_NEG_TABLE ((uintptr_t)(const void *)am2_crt_pow10_neg_table)
#undef ADDR_CRT_HUGE_VAL
#define ADDR_CRT_HUGE_VAL       ((uintptr_t)(const void *)am2_crt_huge_val)

/* The CRT _pctype classification table (conv.cpp), pure const uint16 data; the
 * ADDR_CRT_PCTYPE blob slot still points one entry into it, so it stays. */
#undef ADDR_CRT_CTYPE_TABLE
#define ADDR_CRT_CTYPE_TABLE    ((uintptr_t)(const void *)am2_crt_ctype)

/* The CRT "(null)" printf string slot (printf.cpp), a one-entry char* table. */
#undef ADDR_CRT_NULLSTRING
#define ADDR_CRT_NULLSTRING     ((uintptr_t)(const void *)am2_crt_nullstring)

/* CRT time-zone / DST runtime state (time.cpp), non-const .data-init arrays.
 * The cluster base places the array; the other two fields alias into it. */
#undef ADDR_CRT_TIMEZONE
#define ADDR_CRT_TIMEZONE       ((uintptr_t)(const void *)am2_crt_tz_state)
#undef ADDR_CRT_DAYLIGHT
#define ADDR_CRT_DAYLIGHT       ((uintptr_t)(const void *)&am2_crt_tz_state[1])
#undef ADDR_CRT_DSTBIAS
#define ADDR_CRT_DSTBIAS        ((uintptr_t)(const void *)&am2_crt_tz_state[2])
#undef ADDR_CRT_DST_START_YEAR
#define ADDR_CRT_DST_START_YEAR ((uintptr_t)(const void *)am2_crt_dst_start)
#undef ADDR_CRT_DST_START_YDAY
#define ADDR_CRT_DST_START_YDAY ((uintptr_t)(const void *)&am2_crt_dst_start[1])
#undef ADDR_CRT_DST_START_MS
#define ADDR_CRT_DST_START_MS   ((uintptr_t)(const void *)&am2_crt_dst_start[2])
#undef ADDR_CRT_DST_END_YEAR
#define ADDR_CRT_DST_END_YEAR   ((uintptr_t)(const void *)am2_crt_dst_end)
#undef ADDR_CRT_DST_END_YDAY
#define ADDR_CRT_DST_END_YDAY   ((uintptr_t)(const void *)&am2_crt_dst_end[1])
#undef ADDR_CRT_DST_END_MS
#define ADDR_CRT_DST_END_MS     ((uintptr_t)(const void *)&am2_crt_dst_end[2])

/* More CRT runtime .data-init scalars (conv/startup/fltcvt.cpp). */
#undef ADDR_CRT_MB_CUR_MAX
#define ADDR_CRT_MB_CUR_MAX     ((uintptr_t)(const void *)&am2_crt_mb_cur_max)
#undef ADDR_CRT_APP_TYPE
#define ADDR_CRT_APP_TYPE       ((uintptr_t)(const void *)&am2_crt_app_type)
#undef ADDR_CRT_DECIMAL_POINT
#define ADDR_CRT_DECIMAL_POINT  ((uintptr_t)(const void *)am2_crt_decimal_point)

/* The shared rand seed (rand.cpp), read/written by the CRT and the game. */
extern uint32_t am2_rand_seed;
#undef ADDR_RAND_SEED
#define ADDR_RAND_SEED          ((uintptr_t)(const void *)&am2_rand_seed)

/* The mouse/cursor input-state block (device.cpp): one array, each field
 * aliased to its slot. Read/written across the win32 layer and the C control
 * socket, so the declaration is shared here. */
extern int32_t am2_mouse_state[25];
#undef ADDR_MOUSE_DX
#define ADDR_MOUSE_DX           ((uintptr_t)(const void *)am2_mouse_state)
#undef ADDR_MOUSE_DY
#define ADDR_MOUSE_DY           ((uintptr_t)(const void *)&am2_mouse_state[1])
#undef ADDR_MOUSE_DZ
#define ADDR_MOUSE_DZ           ((uintptr_t)(const void *)&am2_mouse_state[2])
#undef ADDR_CURSOR_X
#define ADDR_CURSOR_X           ((uintptr_t)(const void *)&am2_mouse_state[3])
#undef ADDR_CURSOR_Y
#define ADDR_CURSOR_Y           ((uintptr_t)(const void *)&am2_mouse_state[4])
#undef ADDR_CURSOR_POINT
#define ADDR_CURSOR_POINT       ((uintptr_t)(const void *)&am2_mouse_state[5])
#undef ADDR_MOUSE_BUTTON
#define ADDR_MOUSE_BUTTON       ((uintptr_t)(const void *)&am2_mouse_state[6])
#undef ADDR_MOUSE_BUTTON1
#define ADDR_MOUSE_BUTTON1      ((uintptr_t)(const void *)&am2_mouse_state[7])
#undef ADDR_MOUSE_CHANGED
#define ADDR_MOUSE_CHANGED      ((uintptr_t)(const void *)&am2_mouse_state[9])
#undef ADDR_MOUSE_CHANGED1
#define ADDR_MOUSE_CHANGED1     ((uintptr_t)(const void *)&am2_mouse_state[10])
#undef ADDR_MOUSE_CLAIMED
#define ADDR_MOUSE_CLAIMED      ((uintptr_t)(const void *)&am2_mouse_state[12])
#undef ADDR_MOUSE_MOVED
#define ADDR_MOUSE_MOVED        ((uintptr_t)(const void *)&am2_mouse_state[15])
#undef ADDR_MOUSE_PRESS
#define ADDR_MOUSE_PRESS        ((uintptr_t)(const void *)&am2_mouse_state[16])
#undef ADDR_MOUSE_PRESS_MS
#define ADDR_MOUSE_PRESS_MS     ((uintptr_t)(const void *)&am2_mouse_state[17])
#undef ADDR_MOUSE_PRESS2
#define ADDR_MOUSE_PRESS2       ((uintptr_t)(const void *)&am2_mouse_state[18])
#undef ADDR_MOUSE_PRESS2_MS
#define ADDR_MOUSE_PRESS2_MS    ((uintptr_t)(const void *)&am2_mouse_state[19])
#undef ADDR_MOUSE_ACTIVITY
#define ADDR_MOUSE_ACTIVITY     ((uintptr_t)(const void *)&am2_mouse_state[22])
#undef ADDR_MOUSE_GRAB
#define ADDR_MOUSE_GRAB         ((uintptr_t)(const void *)&am2_mouse_state[23])
#undef ADDR_POINTER_HOVER_UID
#define ADDR_POINTER_HOVER_UID  ((uintptr_t)(const void *)&am2_mouse_state[24])

/* Runtime-written game scalars: screen dims / fog / overlay (frame.cpp, read
 * across the win32 layer) and the debug blast kind (widget.cpp, local). */
extern int32_t am2_screen_w, am2_screen_h, am2_fog_of_war, am2_info_overlay_on;
#undef ADDR_SCREEN_W
#define ADDR_SCREEN_W           ((uintptr_t)(const void *)&am2_screen_w)
#undef ADDR_SCREEN_H
#define ADDR_SCREEN_H           ((uintptr_t)(const void *)&am2_screen_h)
#undef ADDR_FOG_OF_WAR
#define ADDR_FOG_OF_WAR         ((uintptr_t)(const void *)&am2_fog_of_war)
#undef ADDR_INFO_OVERLAY_ON
#define ADDR_INFO_OVERLAY_ON    ((uintptr_t)(const void *)&am2_info_overlay_on)
#undef ADDR_DEBUG_BLAST_KIND
#define ADDR_DEBUG_BLAST_KIND   ((uintptr_t)(const void *)&am2_debug_blast_kind)

/* Default map/script/rules names and dir names (gamedir.cpp), char* slots. */
extern const char *const am2_dir_defaults[3];
extern const char *const am2_dir_names[4];
#undef ADDR_MAP_NAME_DEFAULT
#define ADDR_MAP_NAME_DEFAULT   ((uintptr_t)(const void *)am2_dir_defaults)
#undef ADDR_MP_SCRIPT_DEFAULT
#define ADDR_MP_SCRIPT_DEFAULT  ((uintptr_t)(const void *)&am2_dir_defaults[1])
#undef ADDR_RULES_DIR_STR
#define ADDR_RULES_DIR_STR      ((uintptr_t)(const void *)&am2_dir_defaults[2])
#undef ADDR_STR_AVI_DIR
#define ADDR_STR_AVI_DIR        ((uintptr_t)(const void *)am2_dir_names)
#undef ADDR_WAVE_DIR
#define ADDR_WAVE_DIR           ((uintptr_t)(const void *)&am2_dir_names[1])
#undef ADDR_DIR_TITLE_PTR
#define ADDR_DIR_TITLE_PTR      ((uintptr_t)(const void *)&am2_dir_names[2])
#undef ADDR_AUDIO_PATH_ARG
#define ADDR_AUDIO_PATH_ARG     ((uintptr_t)(const void *)&am2_dir_names[3])

/* The five score-variable name slots (script.cpp); base places, fields alias. */
#undef ADDR_NAME_SCORE_LIMIT
#define ADDR_NAME_SCORE_LIMIT   ((uintptr_t)(const void *)am2_name_score_vars)
#undef ADDR_NAME_GREENSCORE
#define ADDR_NAME_GREENSCORE    ((uintptr_t)(const void *)&am2_name_score_vars[1])
#undef ADDR_NAME_TANSCORE
#define ADDR_NAME_TANSCORE      ((uintptr_t)(const void *)&am2_name_score_vars[2])
#undef ADDR_NAME_BLUESCORE
#define ADDR_NAME_BLUESCORE     ((uintptr_t)(const void *)&am2_name_score_vars[3])
#undef ADDR_NAME_GREYSCORE
#define ADDR_NAME_GREYSCORE     ((uintptr_t)(const void *)&am2_name_score_vars[4])

/* The script "unknown" word (script.cpp) and the edit-field charset (widget.cpp),
 * both read-only char* slots. */
#undef ADDR_SCRIPT_UNKNOWN_STR
#define ADDR_SCRIPT_UNKNOWN_STR ((uintptr_t)(const void *)am2_script_unknown_str)
#undef ADDR_EDIT_CHARSET_DEFAULT
#define ADDR_EDIT_CHARSET_DEFAULT ((uintptr_t)(const void *)am2_edit_charset_default)

/* Small game const tables: the trooper heading-sweep deltas (region.cpp), the
 * airstrike slot records (air.cpp), and the "Sarge" unit-name slot (item.cpp). */
#undef ADDR_STEP_FACING_SWEEP
#define ADDR_STEP_FACING_SWEEP  ((uintptr_t)(const void *)am2_step_facing_sweep)
#undef ADDR_SLOT_RECS
#define ADDR_SLOT_RECS          ((uintptr_t)(const void *)am2_slot_recs)
#undef ADDR_UNIT_NAME_SARGE
#define ADDR_UNIT_NAME_SARGE    ((uintptr_t)(const void *)am2_unit_name_sarge)

/* Code-read blob strings folded to C literals (CRT names, error banners, the
 * multiplayer status lines, team-win names, bad-map messages). Generated by
 * tools/foldstrings.py; the redirects must come after orig.h's ADDR defines,
 * so this is last. deadstrings.py drops the blob copies. */
#include "foldstr.h"

#endif /* AM2_STANDALONE_H */
