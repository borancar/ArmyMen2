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
#include "../game/origstate.h"   /* AM2_OrigState, so redirects can name members */

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
/* The roach (giant ant) creature parameters, 0x00487BA8..0x00487C00, as ONE
 * record (item.cpp). Its first eight dwords are the game-constants block that
 * DefGameParse fills from the .aai by index (ADDR_GAME_CONSTANTS), so it is one
 * object and NOT const -- it used to be thirteen separate const scalars that
 * only formed an indexable block while place.ld laid them end to end in a
 * writable section. Every slot is a dword; health and start_frame are read as
 * int16 out of theirs, height as a byte. */
struct AM2_RoachParams {
    int32_t height;            /* +0x00  game-constants block [0] */
    int32_t health;            /* +0x04  [1], read as int16 */
    int32_t armour;            /* +0x08  [2] */
    int32_t damage;            /* +0x0C  [3] */
    int32_t forvel;            /* +0x10  [4] */
    int32_t revvel;            /* +0x14  [5] */
    int32_t foracc;            /* +0x18  [6] */
    int32_t revacc;            /* +0x1C  [7] */
    int32_t box[4];            /* +0x20  AM2_Rect */
    int32_t bite_box[4];       /* +0x30  AM2_Rect */
    int32_t row_spec[4];       /* +0x40 */
    /* +0x50: seven frame ids indexed by the NEW field-530 state, unbounded
     * (81, 81, 80, 85, 6, 35, 84); [1] is also ROACH_START_FRAME, read as
     * int16. Once two scalars here plus a separate 20-byte
     * am2_roach_score_ints, so states 2..6 read past the struct wherever a
     * linker put it (found by the view-extent scan, not by a crash). */
    int32_t field_530_frames[7];
};
extern struct AM2_RoachParams am2_roach;
/* Object hit-boxes and row-specs, int32[4] rects (item.cpp). */
extern const int32_t am2_explosion_box[4], am2_explosion_area_16[4];
extern const int32_t am2_explosion_area_24[4], am2_explosion_area_32[4];
extern const int32_t am2_explosion_row_spec[4], am2_missile_box[4], am2_missile_row_spec[4];
extern const int32_t am2_trooper_box[4], am2_trooper_row_spec[4];
extern const int32_t am2_vehicle_box[4], am2_vehicle_row_spec[4], am2_kind7_box[4];
/* Gameplay scalar parameters (misc.cpp). am2_pillbox_trooper_health is a view
 * into am2_rank_records (see ADDR_PILLBOX_TROOPER_HEALTH below), not a symbol. */
extern const int16_t am2_seq_k4_rise;
extern const float   am2_gravity, am2_difficulty_scale[3];
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
extern const int16_t am2_hud_cmd_offsets[6];
extern const int16_t am2_hud_squad_slot_xy[24];
extern const int32_t am2_spiral_step[8];
extern uint32_t am2_map_field_descs[14];   /* runtime-written (LoadMap) */
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
/* The HUD command block (widget.cpp), 0x004761A8..0x004766C0, one object. */
struct AM2_HudCommand {                /* 0x28 bytes */
    int32_t     set, index, frame;     /* +0x00  PreloadArmySprite's arguments */
    void       *sprite;                /* +0x0C  stored by HudCmdConstruct */
    const void *pick, *action, *invoke;/* +0x10  the pointer mode's handlers */
    int32_t     a, b, c;               /* +0x1C */
};
struct AM2_HudCommandBlock {
    struct AM2_HudCommand cmd[7];      /* 0x004761A8 */
    int32_t  build_rect0[4];           /* 0x004762C0  button 0's rect */
    uint32_t build_menu[252];          /* 0x004762D0  18 records of 0x38 */
    uint32_t sarge_tail[2];            /* 0x004766C0  HUD_SARGE_OFFSETS cells 4-5 */
};
extern struct AM2_HudCommandBlock am2_hud_commands;
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
/* The neighbouring action rows alias into the same array (base 0x00474FE0). */
#undef ADDR_ACTION_CROUCH
#define ADDR_ACTION_CROUCH      ((uintptr_t)(const void *)&am2_weapon_pose_frames[44])
#undef ADDR_ACTION_PRONE
#define ADDR_ACTION_PRONE       ((uintptr_t)(const void *)&am2_weapon_pose_frames[50])
#undef ADDR_ACTION_KEY1
#define ADDR_ACTION_KEY1        ((uintptr_t)(const void *)&am2_weapon_pose_frames[59])
#undef ADDR_ACTION_KEY4
#define ADDR_ACTION_KEY4        ((uintptr_t)(const void *)&am2_weapon_pose_frames[62])
#undef ADDR_ACTION_KEY5
#define ADDR_ACTION_KEY5        ((uintptr_t)(const void *)&am2_weapon_pose_frames[65])

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
extern int16_t am2_air_path_turn_y_in[1], am2_air_path_turn_y_out[1];
#undef ADDR_AIR_PATH_TURN_Y_IN
#define ADDR_AIR_PATH_TURN_Y_IN  ((uintptr_t)(const void *)am2_air_path_turn_y_in)
#undef ADDR_AIR_PATH_TURN_Y_OUT
#define ADDR_AIR_PATH_TURN_Y_OUT ((uintptr_t)(const void *)am2_air_path_turn_y_out)
/* ADDR_AIR_PATH_TURN_Y_IN / _OUT are runtime state written by AirInitTurnYIn/Out;
 * left at their writable .origdat placement rather than redirected to a const. */

#undef ADDR_ROACH_HEALTH
#define ADDR_ROACH_HEALTH       ((uintptr_t)(const void *)&am2_roach.health)
#undef ADDR_ROACH_ARMOUR
#define ADDR_ROACH_ARMOUR       ((uintptr_t)(const void *)&am2_roach.armour)
#undef ADDR_ROACH_DAMAGE
#define ADDR_ROACH_DAMAGE       ((uintptr_t)(const void *)&am2_roach.damage)
#undef ADDR_ROACH_FORVEL
#define ADDR_ROACH_FORVEL       ((uintptr_t)(const void *)&am2_roach.forvel)
#undef ADDR_ROACH_REVVEL
#define ADDR_ROACH_REVVEL       ((uintptr_t)(const void *)&am2_roach.revvel)
#undef ADDR_ROACH_FORACC
#define ADDR_ROACH_FORACC       ((uintptr_t)(const void *)&am2_roach.foracc)
#undef ADDR_ROACH_REVACC
#define ADDR_ROACH_REVACC       ((uintptr_t)(const void *)&am2_roach.revacc)
#undef ADDR_ROACH_BOX
#define ADDR_ROACH_BOX          ((uintptr_t)(const void *)&am2_roach.box)
#undef ADDR_ROACH_BITE_BOX
#define ADDR_ROACH_BITE_BOX     ((uintptr_t)(const void *)&am2_roach.bite_box)
#undef ADDR_ROACH_ROW_SPEC
#define ADDR_ROACH_ROW_SPEC     ((uintptr_t)(const void *)&am2_roach.row_spec)
#undef ADDR_FIELD_530_FRAMES
#define ADDR_FIELD_530_FRAMES   ((uintptr_t)(const void *)am2_roach.field_530_frames)
#undef ADDR_ROACH_START_FRAME
#define ADDR_ROACH_START_FRAME  ((uintptr_t)(const void *)&am2_roach.field_530_frames[1])

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

/* Record 4's max-health field in am2_rank_records (declared with the rank block
 * below) -- the image aliases the pillbox trooper's health onto it. */
#undef ADDR_PILLBOX_TROOPER_HEALTH
#define ADDR_PILLBOX_TROOPER_HEALTH ((uintptr_t)(const void *)&am2_rank_records[132])
#undef ADDR_GRAVITY
#define ADDR_GRAVITY            ((uintptr_t)(const void *)&am2_gravity)
#undef ADDR_VIEW_SPEED
#define ADDR_VIEW_SPEED         ((uintptr_t)(const void *)&am2_view_speed)
/* ADDR_TICK_INTERVAL_MS is runtime state (ResetLevelState); not redirected. */
#undef ADDR_DIFFICULTY_SCALE
#define ADDR_DIFFICULTY_SCALE   ((uintptr_t)(const void *)am2_difficulty_scale)
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
#define ADDR_HUD_SARGE_OFFSETS  ((uintptr_t)(const void *)&am2_hud_commands.build_menu[248])  /* tail view */
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
#define ADDR_POINTER_MODES      ((uintptr_t)(const void *)&am2_hud_commands.cmd[0].pick)

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

/* The CRT _pctype classification table (conv.cpp) and its pointer slot (which
 * points one entry into it), plus the wide "(null)" slot (printf.cpp). */
#undef ADDR_CRT_CTYPE_TABLE
#define ADDR_CRT_CTYPE_TABLE    ((uintptr_t)(const void *)am2_crt_ctype)
#undef ADDR_CRT_PCTYPE
#define ADDR_CRT_PCTYPE         ((uintptr_t)(const void *)&am2_crt_pctype)
#undef ADDR_CRT_WNULLSTRING
#define ADDR_CRT_WNULLSTRING    ((uintptr_t)(const void *)&am2_crt_wnullstring)

/* The DirectInput buffer-size property struct (device.cpp), const. */
#undef ADDR_DIPROP_BUFFER_SIZE
#define ADDR_DIPROP_BUFFER_SIZE ((uintptr_t)(const void *)am2_diprop_buffer_size)
/* The image's DIDATAFORMAT structs (device.cpp), placed byte-identically so the
 * blob drops them; native reads the SDK's own structs, these only the hybrid. */
#undef ADDR_DF_KEYBOARD
#define ADDR_DF_KEYBOARD        ((uintptr_t)(const void *)am2_df_keyboard)
#undef ADDR_DF_MOUSE
#define ADDR_DF_MOUSE           ((uintptr_t)(const void *)am2_df_mouse)

/* The CRT "(null)" printf string slot (printf.cpp), a one-entry char* table. */
#undef ADDR_CRT_NULLSTRING
#define ADDR_CRT_NULLSTRING     ((uintptr_t)(const void *)am2_crt_nullstring)
/* crt_output's format state-machine table (printf.cpp), pure const bytes. */
#undef ADDR_CRT_PRINTF_TABLE
#define ADDR_CRT_PRINTF_TABLE   ((uintptr_t)(const void *)am2_crt_printf_table)
#undef ADDR_CRT_RTERR_POOL
#define ADDR_CRT_RTERR_POOL     ((uintptr_t)(const void *)am2_crt_rterr_pool)

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

/* CRT init/term function-pointer tables (startup.cpp); base places, ends alias.
 * A table of the reconstructions themselves -- crt_cinit's initterm walk IS
 * reached natively and calls straight through it, so no startup fixup. */
extern const am2_init_fn am2_crt_inittab[36];
#undef ADDR_CRT_XC_BEGIN
#define ADDR_CRT_XC_BEGIN       ((uintptr_t)(const void *)am2_crt_inittab)
#undef ADDR_CRT_XC_END
#define ADDR_CRT_XC_END         ((uintptr_t)(const void *)&am2_crt_inittab[22])
#undef ADDR_CRT_XI_BEGIN
#define ADDR_CRT_XI_BEGIN       ((uintptr_t)(const void *)&am2_crt_inittab[23])
#undef ADDR_CRT_XI_END
#define ADDR_CRT_XI_END         ((uintptr_t)(const void *)&am2_crt_inittab[28])
#undef ADDR_CRT_XP_BEGIN
#define ADDR_CRT_XP_BEGIN       ((uintptr_t)(const void *)&am2_crt_inittab[29])
#undef ADDR_CRT_XP_END
#define ADDR_CRT_XP_END         ((uintptr_t)(const void *)&am2_crt_inittab[31])
#undef ADDR_CRT_XT_BEGIN
#define ADDR_CRT_XT_BEGIN       ((uintptr_t)(const void *)&am2_crt_inittab[32])
#undef ADDR_CRT_XT_END
#define ADDR_CRT_XT_END         ((uintptr_t)(const void *)&am2_crt_inittab[34])

/* CRT Pentium-FDIV self-test doubles (startup.cpp) and small-block-heap
 * threshold (heap.cpp), each read in one TU. */
#undef ADDR_CRT_FDIV_NUM
#define ADDR_CRT_FDIV_NUM       ((uintptr_t)(const void *)&am2_crt_fdiv_num)
#undef ADDR_CRT_FDIV_DEN
#define ADDR_CRT_FDIV_DEN       ((uintptr_t)(const void *)&am2_crt_fdiv_den)
#undef ADDR_CRT_SBH_THRESHOLD
#define ADDR_CRT_SBH_THRESHOLD  ((uintptr_t)(const void *)&am2_crt_sbh_threshold)

/* CRT pointer slots: _fpinit/_exit (startup.cpp, dead image ptrs), the _fltin2
 * result pointer (strtod.cpp), and the __badioinfo sentinel (lowio.cpp). */
#undef ADDR_CRT_FPINIT_PTR
#define ADDR_CRT_FPINIT_PTR     ((uintptr_t)(const void *)am2_crt_fpinit_ptr)
#undef ADDR_CRT_EXIT_FN_PTR
#define ADDR_CRT_EXIT_FN_PTR    ((uintptr_t)(const void *)am2_crt_exit_fn_ptr)
#undef ADDR_CRT_FLT_PTR
#define ADDR_CRT_FLT_PTR        ((uintptr_t)(const void *)&am2_crt_flt_ptr)
#undef ADDR_CRT_BADIOINFO
#define ADDR_CRT_BADIOINFO      ((uintptr_t)(const void *)am2_crt_badioinfo)
/* The _iob FILE array (stdio.cpp), non-const runtime state. */
#undef ADDR_CRT_IOB
#define ADDR_CRT_IOB            ((uintptr_t)(const void *)am2_crt_iob)

/* The load-filled sprite-slot pointer slots (sprite.cpp), read across the game. */
extern uint32_t am2_seq_sprites_7[1], am2_decal_sprites[1], am2_seq_sprites_5[1];
extern uint32_t am2_mark_sprites[1], am2_mp_mark_grid[1];
#undef ADDR_SEQ_SPRITES_7
#define ADDR_SEQ_SPRITES_7      ((uintptr_t)(const void *)am2_seq_sprites_7)
#undef ADDR_DECAL_SPRITES
#define ADDR_DECAL_SPRITES      ((uintptr_t)(const void *)am2_decal_sprites)
#undef ADDR_SEQ_SPRITES_5
#define ADDR_SEQ_SPRITES_5      ((uintptr_t)(const void *)am2_seq_sprites_5)
#undef ADDR_MARK_SPRITES
#define ADDR_MARK_SPRITES       ((uintptr_t)(const void *)am2_mark_sprites)
#undef ADDR_MP_MARK_GRID
#define ADDR_MP_MARK_GRID       ((uintptr_t)(const void *)am2_mp_mark_grid)
/* The sprite-group definition table (base, +4 PARTICLE_KINDS, two end bounds)
 * and the sprite-grid pointer slot (sprite.cpp; PARTICLE_KINDS read in maprow). */
extern uint8_t am2_sprite_groups[552];   /* runtime-written sprite slots */
extern uint32_t am2_sprite_grid[1];
#undef ADDR_SPRITE_GROUPS
#define ADDR_SPRITE_GROUPS      ((uintptr_t)(const void *)am2_sprite_groups)
#undef ADDR_PARTICLE_KINDS
#define ADDR_PARTICLE_KINDS     ((uintptr_t)(const void *)&am2_sprite_groups[4])
#undef ADDR_SPRITE_GROUPS_END
#define ADDR_SPRITE_GROUPS_END  ((uintptr_t)(const void *)&am2_sprite_groups[0xF0])
#undef ADDR_SPRITE_GROUPS_B_END
#define ADDR_SPRITE_GROUPS_B_END ((uintptr_t)(const void *)&am2_sprite_groups[0x228])
#undef ADDR_SPRITE_GRID
#define ADDR_SPRITE_GRID        ((uintptr_t)(const void *)am2_sprite_grid)

/* The -df debug-mode flag (misc.cpp), read across the win32 layer. */
extern uint32_t am2_opt_df;
#undef ADDR_OPT_DF
#define ADDR_OPT_DF             ((uintptr_t)(const void *)&am2_opt_df)

/* The roach game-constants BLOCK base at 0x00487BA8 (gameproc.cpp's kGameConst
 * writes indices 0..7 from the .aai; item.cpp reads index 0 as ROACH_HEIGHT):
 * the first eight dwords of am2_roach, one object, so the indexing stays
 * inside it wherever the linker puts it. */
#undef ADDR_GAME_CONSTANTS
#define ADDR_GAME_CONSTANTS     ((uintptr_t)(const void *)&am2_roach)

/* The rank records (item.cpp): eight 28-byte runtime-written records at
 * 0x00473DC0, placed as one 224-byte array. ADDR_RANK_RECORDS is the base;
 * ADDR_RANK_EXP_TABLE is the +0x18 XP-field view of the same records (stride 28)
 * and ADDR_PILLBOX_TROOPER_HEALTH (above) is record 4's max-health field at
 * +132 -- both alias into this array. */
extern uint8_t am2_rank_records[224];
#undef ADDR_RANK_RECORDS
#define ADDR_RANK_RECORDS       ((uintptr_t)(const void *)am2_rank_records)
#undef ADDR_RANK_EXP_TABLE
#define ADDR_RANK_EXP_TABLE     ((uintptr_t)(const void *)&am2_rank_records[0x18])

/* The dead {WriteDotString, "FOO"} record (misc.cpp); nothing reads it, it is
 * placed so its handler slot needs no startup fixup. */
extern const AM2_WriteDotRecord am2_write_dot_record[1];
#undef ADDR_WRITE_DOT_RECORD
#define ADDR_WRITE_DOT_RECORD   ((uintptr_t)(const void *)am2_write_dot_record)

/* The comm-object slot / army table / enum count (commmsg.cpp), read across
 * the game; base aliases ARMY_TABLE and COMM_OBJECT, the count is at +1. */
struct AM2_CommObjectSlot {
    void   *obj;         /* 0x004751B0: the comm object (am2_origstate.comm_global) */
    int32_t enum_count;  /* 0x004751B4: seeds at 999 */
};
extern struct AM2_CommObjectSlot am2_comm_object;
#undef ADDR_ARMY_TABLE
#define ADDR_ARMY_TABLE         ((uintptr_t)(const void *)&am2_comm_object)  /* == &.obj */
#undef ADDR_COMM_OBJECT
#define ADDR_COMM_OBJECT        ((uintptr_t)(const void *)&am2_comm_object)  /* == &.obj */
#undef ADDR_COMM_ENUM_COUNT
#define ADDR_COMM_ENUM_COUNT    ((uintptr_t)(const void *)&am2_comm_object.enum_count)

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

/* The screen-geometry block (device.cpp): one array, overlapping RECT/scalar
 * views aliased to their slots. Read across the win32 layer. */
extern int32_t am2_screen_geom[12];
#undef ADDR_SCREEN_CLIP
#define ADDR_SCREEN_CLIP        ((uintptr_t)(const void *)am2_screen_geom)
#undef ADDR_BITMAP_AREA_W
#define ADDR_BITMAP_AREA_W      ((uintptr_t)(const void *)&am2_screen_geom[2])
#undef ADDR_BITMAP_AREA_H
#define ADDR_BITMAP_AREA_H      ((uintptr_t)(const void *)&am2_screen_geom[3])
#undef ADDR_BLIT_RECT
#define ADDR_BLIT_RECT          ((uintptr_t)(const void *)&am2_screen_geom[4])
#undef ADDR_HUD_PANEL_X
#define ADDR_HUD_PANEL_X        ((uintptr_t)(const void *)&am2_screen_geom[6])
#undef ADDR_ORIGIN_DX
#define ADDR_ORIGIN_DX          ((uintptr_t)(const void *)&am2_screen_geom[8])
#undef ADDR_SCREEN_RECT
#define ADDR_SCREEN_RECT        ((uintptr_t)(const void *)&am2_screen_geom[8])
#undef ADDR_ORIGIN_DY
#define ADDR_ORIGIN_DY          ((uintptr_t)(const void *)&am2_screen_geom[9])

/* Path-search state (region.cpp), palette-cycle index (palette.cpp), and the
 * frame tick interval (gameproc.cpp, also read by item.cpp). */
#undef ADDR_PATH_MAX_NODES
#define ADDR_PATH_MAX_NODES     ((uintptr_t)(const void *)&am2_path_max_nodes)
#undef ADDR_PATH_RESUME
#define ADDR_PATH_RESUME        ((uintptr_t)(const void *)&am2_path_resume)
#undef ADDR_REGION_SEARCH_STATE
#define ADDR_REGION_SEARCH_STATE ((uintptr_t)(const void *)&am2_region_search_state)
#undef ADDR_PALETTE_CYCLE_INDEX
#define ADDR_PALETTE_CYCLE_INDEX ((uintptr_t)(const void *)&am2_palette_cycle_index)
/* Runtime-written palette/charset pointer slots (palette.cpp / widget.cpp):
 * real pointers, initialised to the am2_origstate buffers / placed charset. */
extern void *am2_active_palette, *am2_remap_tint;
#undef ADDR_ACTIVE_PALETTE
#define ADDR_ACTIVE_PALETTE     ((uintptr_t)(const void *)&am2_active_palette)
#undef ADDR_MOVIE_PALETTE_OWNER
#define ADDR_MOVIE_PALETTE_OWNER ((uintptr_t)(const void *)&am2_active_palette)
#undef ADDR_REMAP_TINT
#define ADDR_REMAP_TINT         ((uintptr_t)(const void *)&am2_remap_tint)
#undef ADDR_EDIT_CHARSET_PTR
#define ADDR_EDIT_CHARSET_PTR   ((uintptr_t)(const void *)&am2_edit_charset_ptr)
/* am2_edit_charset_ptr (above) still holds 0x00485360; placing the string there
 * as C makes that raw pointer resolve without the carried blob. */
#undef ADDR_EDIT_CHARSET_STR
#define ADDR_EDIT_CHARSET_STR   ((uintptr_t)(const void *)am2_edit_charset)
#undef ADDR_CRT_TZNAME
#define ADDR_CRT_TZNAME         ((uintptr_t)(const void *)am2_crt_tzname)

/* The LOGPALETTE block (palette.cpp): base places it, entries alias at +4. */
extern uint8_t am2_logpalette[1028];
#undef ADDR_LOGPALETTE
#define ADDR_LOGPALETTE         ((uintptr_t)(const void *)am2_logpalette)
#undef ADDR_LOGPALETTE_ENTRIES
#define ADDR_LOGPALETTE_ENTRIES ((uintptr_t)(const void *)&am2_logpalette[4])
extern uint8_t am2_gdi_palette[1024];
#undef ADDR_GDI_PALETTE
#define ADDR_GDI_PALETTE        ((uintptr_t)(const void *)am2_gdi_palette)

/* The import address table (runtime.cpp): base places the 180-slot array,
 * imports.cpp fills it by raw address, reconstructed code reads a few slots. */
extern uint8_t am2_iat[720];
#undef ADDR_IAT_BASE
#define ADDR_IAT_BASE           ((uintptr_t)(const void *)am2_iat)
/* The IAT_* spellings of four slots (orig.h) reached them by raw VA, bypassing
 * the ADDR_IAT_* redirects below; routed into am2_iat the same way. */
#undef IAT_GET_TICK_COUNT
#define IAT_GET_TICK_COUNT      ((uintptr_t)(const void *)&am2_iat[0x084])
#undef IAT_WAIT_FOR_SINGLE_OBJECT
#define IAT_WAIT_FOR_SINGLE_OBJECT ((uintptr_t)(const void *)&am2_iat[0x080])
#undef IAT_RELEASE_MUTEX
#define IAT_RELEASE_MUTEX       ((uintptr_t)(const void *)&am2_iat[0x060])
#undef IAT_POST_MESSAGE_A
#define IAT_POST_MESSAGE_A      ((uintptr_t)(const void *)&am2_iat[0x1CC])
#undef ADDR_IAT_GET_TICK_COUNT
#define ADDR_IAT_GET_TICK_COUNT ((uintptr_t)(const void *)&am2_iat[0x84])
#undef ADDR_IAT_INTERSECT_RECT
#define ADDR_IAT_INTERSECT_RECT ((uintptr_t)(const void *)&am2_iat[0x258])
#undef ADDR_IAT_SMACK_VOLUMEPAN
#define ADDR_IAT_SMACK_VOLUMEPAN ((uintptr_t)(const void *)&am2_iat[0x2AC])
#undef ADDR_IAT_SMACK_TO_BUFFER
#define ADDR_IAT_SMACK_TO_BUFFER ((uintptr_t)(const void *)&am2_iat[0x2B0])
#undef ADDR_IAT_SMACK_DO_FRAME
#define ADDR_IAT_SMACK_DO_FRAME  ((uintptr_t)(const void *)&am2_iat[0x2B4])
#undef ADDR_IAT_SMACK_NEXT_FRAME
#define ADDR_IAT_SMACK_NEXT_FRAME ((uintptr_t)(const void *)&am2_iat[0x2B8])
#undef ADDR_IAT_SMACK_WAIT
#define ADDR_IAT_SMACK_WAIT      ((uintptr_t)(const void *)&am2_iat[0x2BC])
#undef ADDR_IAT_SMACK_CLOSE
#define ADDR_IAT_SMACK_CLOSE     ((uintptr_t)(const void *)&am2_iat[0x2C0])
#undef ADDR_IAT_SMACK_USE_DSOUND
#define ADDR_IAT_SMACK_USE_DSOUND ((uintptr_t)(const void *)&am2_iat[0x2C4])
#undef ADDR_IAT_SMACK_OPEN
#define ADDR_IAT_SMACK_OPEN      ((uintptr_t)(const void *)&am2_iat[0x2C8])
#undef ADDR_IAT_SMACK_DDTYPE
#define ADDR_IAT_SMACK_DDTYPE    ((uintptr_t)(const void *)&am2_iat[0x2CC])

/* The comm state block (dplay.cpp): message-list heads, event handles, and the
 * packet pool head (which continues into origbss). Base places it; each field
 * aliases to its slot. Read by dplay.cpp and commmsg.cpp. */
extern uint8_t am2_comm_block[1832];
#undef ADDR_MSG_LIST_SENDQ
#define ADDR_MSG_LIST_SENDQ     ((uintptr_t)(const void *)am2_comm_block)
#undef ADDR_MSG_LIST_POOL
#define ADDR_MSG_LIST_POOL      ((uintptr_t)(const void *)&am2_comm_block[0x10])
#undef ADDR_COMM_EVENT
#define ADDR_COMM_EVENT         ((uintptr_t)(const void *)&am2_comm_block[0x20])
#undef ADDR_PACKET_EVENT_A
#define ADDR_PACKET_EVENT_A     ((uintptr_t)(const void *)&am2_comm_block[0x20])
#undef ADDR_COMM_EVENT_2
#define ADDR_COMM_EVENT_2       ((uintptr_t)(const void *)&am2_comm_block[0x24])
#undef ADDR_PACKET_EVENT_B
#define ADDR_PACKET_EVENT_B     ((uintptr_t)(const void *)&am2_comm_block[0x24])
#undef ADDR_PACKET_BUFFERS
#define ADDR_PACKET_BUFFERS     ((uintptr_t)(const void *)&am2_comm_block[0xA0])
extern int32_t am2_tick_interval_ms;
#undef ADDR_TICK_INTERVAL_MS
#define ADDR_TICK_INTERVAL_MS   ((uintptr_t)(const void *)&am2_tick_interval_ms)
/* The rebindable key bindings (gameproc.cpp), walked as bytes; END is the bound. */
extern uint8_t am2_key_bindings[42];
#undef ADDR_KEY_BINDINGS
#define ADDR_KEY_BINDINGS       ((uintptr_t)(const void *)am2_key_bindings)
#undef ADDR_KEY_BINDINGS_END
#define ADDR_KEY_BINDINGS_END   ((uintptr_t)(const void *)&am2_key_bindings[42])

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
/* HUD_CMD_SPEC record 0 (widget.cpp); records 1-6 fall through into the placed
 * pointer_modes storage. HUD_CMD_SPRITES is its sprite field at +0xC. */
#undef ADDR_HUD_CMD_SPEC
#define ADDR_HUD_CMD_SPEC       ((uintptr_t)(const void *)&am2_hud_commands)
#undef ADDR_HUD_CMD_SPRITES
#define ADDR_HUD_CMD_SPRITES    ((uintptr_t)(const void *)&am2_hud_commands.cmd[0].sprite)
/* One past the seventh record's sprite field: the loops step by the record
 * stride and stop here, inside the block (it lands in build_rect0). */
#undef ADDR_HUD_CMD_SPRITES_END
#define ADDR_HUD_CMD_SPRITES_END ((uintptr_t)(const void *)&am2_hud_commands.build_rect0[3])
#undef ADDR_BUILD_MENU_RECTS
#define ADDR_BUILD_MENU_RECTS   ((uintptr_t)(const void *)am2_hud_commands.build_rect0)
/* The build-menu table: am2_hud_commands.build_menu (18 records of 0x38). */
#undef ADDR_BUILD_MENU
#define ADDR_BUILD_MENU         ((uintptr_t)(const void *)am2_hud_commands.build_menu)
#undef ADDR_BUILD_MENU_END
#define ADDR_BUILD_MENU_END     ((uintptr_t)(const void *)&am2_hud_commands.build_menu[252])

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

/* Residual .rdata short strings placed at their VAs by src/standalone/
 * origtail.cpp, so the carried blob reaches zero non-zero unplaced bytes. Each
 * redirect points its ADDR_ macro at the placed pool (which sits at the same
 * VA, so the value is unchanged). A folded macro among these is re-redirected
 * by foldstr.h below to its literal -- the pool is still placed and verified,
 * the compiled read uses the literal; both are the same bytes at the same VA. */
#ifdef __cplusplus
extern "C" {
#endif
extern const uint8_t am2_str_isprocessorfeaturepresent[26];
extern const uint8_t am2_crt_wnull_paren[14];
extern const uint8_t am2_crt_rterr_crlf2[3];
extern const uint8_t am2_crt_rterr_ellipsis[4];
extern const uint8_t am2_crt_wday_names[22];
extern const uint8_t am2_crt_month_names[37];
extern const uint8_t am2_crt_str_tz[3];
extern const uint8_t am2_str_aai_dir[4];
extern const uint8_t am2_mode_rb[3];
extern const uint8_t am2_str_dash_w[3];
extern const uint8_t am2_cmdline_movie_flags[16];
extern const uint8_t am2_bool_word_names[16];
extern const uint8_t am2_str_def_file_mode[3];
extern const uint8_t am2_str_tan[4];
extern const uint8_t am2_mode_wb[3];
extern const uint8_t am2_str_fmt_s[3];
extern const uint8_t am2_str_movie_its[4];
extern const uint8_t am2_str_movie_3do[4];
extern const uint8_t am2_str_movie_sml[4];
extern const uint8_t am2_str_fmt_c[3];
extern const uint8_t am2_str_any_file[4];
extern const uint8_t am2_str_udf_pair[8];
extern const uint8_t am2_flt_230[4];
extern const uint8_t am2_dbl_0p7[8];
extern const uint8_t am2_dbl_consts_a[24];
extern const uint8_t am2_dbl_32[8];
extern const uint8_t am2_flt_100_80[8];
extern const uint8_t am2_fp_consts_b[16];
extern const uint8_t am2_crt_eh_catch_1[24];
extern const uint8_t am2_crt_eh_catch_2[12];
extern const uint8_t am2_crt_eh_catch_3[24];
extern const uint8_t am2_crt_xcpt_tables[136];
extern const uint8_t am2_air_bit_masks[40];
extern const uint32_t am2_mp_pause_bits_a[4];
extern const uint32_t am2_mp_pause_bits_b[4];
extern const uint32_t am2_mp_pause_bits_c[4];
extern const uint8_t am2_map_field_tables[316];
extern const uint8_t am2_spiral_path_const[64];
extern const uint8_t am2_rank_thresholds[32];
extern uint8_t am2_tzname_std[64];
extern uint8_t am2_tzname_dst[64];
extern uint8_t am2_sprite_group_b[48];   /* groups C, runtime-written */
extern const uint8_t am2_sprite_grid_data[324];
extern const uint8_t am2_item_type_offsets[16];
extern const uint8_t am2_screen_consts[8];
extern const uint8_t am2_crt_osver_pair[8];
extern const uint8_t am2_str_computer_slot[15];
extern const uint8_t am2_str_open_slot[11];
extern const uint8_t am2_str_unique_suffix[6];
extern const uint8_t am2_dbl_1_5[8];
extern const uint8_t am2_crt_sig_handlers_a[28];
extern const uint8_t am2_crt_sig_handlers_b[16];
extern const uint8_t am2_crt_pctype_alias[4];
extern const uint8_t am2_str_newline[1];
extern const uint8_t am2_str_glob_all[1];
extern const uint8_t am2_str_line_break[1];
extern const uint8_t am2_key_action_defaults[18];
extern const uint8_t am2_write_dot_flag[4];
extern const uint8_t am2_write_dot_float[4];
extern const uint8_t am2_str_dot[1];
extern const uint8_t am2_bool_display_strings[40];
extern const uint8_t am2_crt_eh_magic[4];
extern const uint8_t am2_palette_cycle_extra[4];
extern const uint8_t am2_debug_blast_flag[4];
extern const uint8_t am2_seq_variant_stride[4];
extern const uint8_t am2_crt_one[4];
extern const uint8_t am2_logpalette_hdr_small[4];
#ifdef __cplusplus
}
#endif
#undef ADDR_STR_ISPROCESSORFEATUREPRESENT
#define ADDR_STR_ISPROCESSORFEATUREPRESENT ((uintptr_t)(const void *)am2_str_isprocessorfeaturepresent)
#undef ADDR_CRT_WNULL_PAREN
#define ADDR_CRT_WNULL_PAREN               ((uintptr_t)(const void *)am2_crt_wnull_paren)
#undef ADDR_STR_RTERR_CRLF2
#define ADDR_STR_RTERR_CRLF2               ((uintptr_t)(const void *)am2_crt_rterr_crlf2)
#undef ADDR_STR_RTERR_ELLIPSIS
#define ADDR_STR_RTERR_ELLIPSIS            ((uintptr_t)(const void *)am2_crt_rterr_ellipsis)
#undef ADDR_CRT_WDAY_NAMES
#define ADDR_CRT_WDAY_NAMES                ((uintptr_t)(const void *)am2_crt_wday_names)
#undef ADDR_CRT_MONTH_NAMES
#define ADDR_CRT_MONTH_NAMES               ((uintptr_t)(const void *)am2_crt_month_names)
#undef ADDR_CRT_STR_TZ
#define ADDR_CRT_STR_TZ                    ((uintptr_t)(const void *)am2_crt_str_tz)
#undef ADDR_STR_AAI_DIR
#define ADDR_STR_AAI_DIR                   ((uintptr_t)(const void *)am2_str_aai_dir)
#undef ADDR_MODE_RB
#define ADDR_MODE_RB                       ((uintptr_t)(const void *)am2_mode_rb)
#undef ADDR_STR_DASH_W
#define ADDR_STR_DASH_W                    ((uintptr_t)(const void *)am2_str_dash_w)
#undef ADDR_CMDLINE_MOVIE_FLAGS
#define ADDR_CMDLINE_MOVIE_FLAGS           ((uintptr_t)(const void *)am2_cmdline_movie_flags)
#undef ADDR_BOOL_WORD_NAMES
#define ADDR_BOOL_WORD_NAMES               ((uintptr_t)(const void *)am2_bool_word_names)
#undef ADDR_STR_DEF_FILE_MODE
#define ADDR_STR_DEF_FILE_MODE             ((uintptr_t)(const void *)am2_str_def_file_mode)
#undef ADDR_STR_TAN
#define ADDR_STR_TAN                       ((uintptr_t)(const void *)am2_str_tan)
#undef ADDR_MODE_WB
#define ADDR_MODE_WB                       ((uintptr_t)(const void *)am2_mode_wb)
#undef ADDR_STR_FMT_S
#define ADDR_STR_FMT_S                     ((uintptr_t)(const void *)am2_str_fmt_s)
#undef ADDR_STR_MOVIE_ITS
#define ADDR_STR_MOVIE_ITS                 ((uintptr_t)(const void *)am2_str_movie_its)
#undef ADDR_STR_MOVIE_3DO
#define ADDR_STR_MOVIE_3DO                 ((uintptr_t)(const void *)am2_str_movie_3do)
#undef ADDR_STR_MOVIE_SMALL
#define ADDR_STR_MOVIE_SMALL               ((uintptr_t)(const void *)am2_str_movie_sml)
#undef ADDR_STR_FMT_C
#define ADDR_STR_FMT_C                     ((uintptr_t)(const void *)am2_str_fmt_c)
#undef ADDR_STR_ANY_FILE
#define ADDR_STR_ANY_FILE                  ((uintptr_t)(const void *)am2_str_any_file)
#undef ADDR_STR_UDF_PAIR
#define ADDR_STR_UDF_PAIR                  ((uintptr_t)(const void *)am2_str_udf_pair)
#undef ADDR_FLT_230
#define ADDR_FLT_230                       ((uintptr_t)(const void *)am2_flt_230)
#undef ADDR_DBL_0P7
#define ADDR_DBL_0P7                       ((uintptr_t)(const void *)am2_dbl_0p7)
#undef ADDR_DBL_CONSTS_A
#define ADDR_DBL_CONSTS_A                  ((uintptr_t)(const void *)am2_dbl_consts_a)
#undef ADDR_DBL_32
#define ADDR_DBL_32                        ((uintptr_t)(const void *)am2_dbl_32)
#undef ADDR_FLT_100_80
#define ADDR_FLT_100_80                    ((uintptr_t)(const void *)am2_flt_100_80)
#undef ADDR_FP_CONSTS_B
#define ADDR_FP_CONSTS_B                   ((uintptr_t)(const void *)am2_fp_consts_b)
#undef ADDR_CRT_EH_CATCH_1
#define ADDR_CRT_EH_CATCH_1                ((uintptr_t)(const void *)am2_crt_eh_catch_1)
#undef ADDR_CRT_EH_CATCH_2
#define ADDR_CRT_EH_CATCH_2                ((uintptr_t)(const void *)am2_crt_eh_catch_2)
#undef ADDR_CRT_EH_CATCH_3
#define ADDR_CRT_EH_CATCH_3                ((uintptr_t)(const void *)am2_crt_eh_catch_3)
#undef ADDR_CRT_XCPT_TABLES
#define ADDR_CRT_XCPT_TABLES               ((uintptr_t)(const void *)am2_crt_xcpt_tables)
#undef ADDR_AIR_BIT_MASKS
#define ADDR_AIR_BIT_MASKS                 ((uintptr_t)(const void *)am2_air_bit_masks)
#undef ADDR_MP_PAUSE_BITS_A
#define ADDR_MP_PAUSE_BITS_A               ((uintptr_t)(const void *)am2_mp_pause_bits_a)
#undef ADDR_MP_PAUSE_BITS_B
#define ADDR_MP_PAUSE_BITS_B               ((uintptr_t)(const void *)am2_mp_pause_bits_b)
#undef ADDR_MP_PAUSE_BITS_C
#define ADDR_MP_PAUSE_BITS_C               ((uintptr_t)(const void *)am2_mp_pause_bits_c)
#undef ADDR_MAP_FIELD_TABLES
#define ADDR_MAP_FIELD_TABLES              ((uintptr_t)(const void *)am2_map_field_tables)
#undef ADDR_SPIRAL_PATH_CONST
#define ADDR_SPIRAL_PATH_CONST             ((uintptr_t)(const void *)am2_spiral_path_const)
#undef ADDR_RANK_THRESHOLDS
#define ADDR_RANK_THRESHOLDS               ((uintptr_t)(const void *)am2_rank_thresholds)
#undef ADDR_TZNAME_STD
#define ADDR_TZNAME_STD                    ((uintptr_t)(const void *)am2_tzname_std)
#undef ADDR_TZNAME_DST
#define ADDR_TZNAME_DST                    ((uintptr_t)(const void *)am2_tzname_dst)
/* Groups C's own table (NOT ADDR_SPRITE_GROUPS_B_END, which is the end of the
 * A+B table above: redefining B_END here sent the group-B loop from
 * am2_sprite_groups off to wherever this separate symbol landed). */
#undef ADDR_SPRITE_GROUPS_C
#define ADDR_SPRITE_GROUPS_C               ((uintptr_t)(const void *)am2_sprite_group_b)
/* ...and its end, which the loaders use as a bound: one past the array, not
 * the raw VA of whatever the linker put next (am2_sprite_grid_rows). */
#undef ADDR_SPRITE_GROUPS_C_END
#define ADDR_SPRITE_GROUPS_C_END           ((uintptr_t)(const void *)&am2_sprite_group_b[48])
#undef ADDR_SPRITE_GRID_DATA
#define ADDR_SPRITE_GRID_DATA              ((uintptr_t)(const void *)am2_sprite_grid_data)
#undef ADDR_ITEM_TYPE_OFFSETS
#define ADDR_ITEM_TYPE_OFFSETS             ((uintptr_t)(const void *)am2_item_type_offsets)
#undef ADDR_DIFFICULTY_CONSTS
#define ADDR_DIFFICULTY_CONSTS             ((uintptr_t)(const void *)&am2_difficulty_scale[1])  /* the table's tail */
#undef ADDR_SCREEN_CONSTS
#define ADDR_SCREEN_CONSTS                 ((uintptr_t)(const void *)am2_screen_consts)
#undef ADDR_CRT_OSVER_PAIR
#define ADDR_CRT_OSVER_PAIR                ((uintptr_t)(const void *)am2_crt_osver_pair)
#undef AM2_STR_COMPUTER_SLOT
#define AM2_STR_COMPUTER_SLOT              ((uintptr_t)(const void *)am2_str_computer_slot)
#undef AM2_STR_OPEN_SLOT
#define AM2_STR_OPEN_SLOT                  ((uintptr_t)(const void *)am2_str_open_slot)
#undef AM2_STR_UNIQUE_SUFFIX
#define AM2_STR_UNIQUE_SUFFIX              ((uintptr_t)(const void *)am2_str_unique_suffix)
#undef AM2_CONST_1_5
#define AM2_CONST_1_5                      ((uintptr_t)(const void *)am2_dbl_1_5)
#undef ADDR_CRT_SIG_HANDLERS_A
#define ADDR_CRT_SIG_HANDLERS_A            ((uintptr_t)(const void *)am2_crt_sig_handlers_a)
#undef ADDR_CRT_SIG_HANDLERS_B
#define ADDR_CRT_SIG_HANDLERS_B            ((uintptr_t)(const void *)am2_crt_sig_handlers_b)
#undef ADDR_CRT_PCTYPE_ALIAS
#define ADDR_CRT_PCTYPE_ALIAS              ((uintptr_t)(const void *)am2_crt_pctype_alias)
#undef ADDR_STR_NEWLINE
#define ADDR_STR_NEWLINE                   ((uintptr_t)(const void *)am2_str_newline)
#undef ADDR_STR_GLOB_ALL
#define ADDR_STR_GLOB_ALL                  ((uintptr_t)(const void *)am2_str_glob_all)
#undef ADDR_STR_LINE_BREAK
#define ADDR_STR_LINE_BREAK                ((uintptr_t)(const void *)am2_str_line_break)
#undef ADDR_KEY_ACTION_DEFAULTS
#define ADDR_KEY_ACTION_DEFAULTS           ((uintptr_t)(const void *)am2_key_action_defaults)
#undef ADDR_WRITE_DOT_FLAG
#define ADDR_WRITE_DOT_FLAG                ((uintptr_t)(const void *)am2_write_dot_flag)
#undef ADDR_WRITE_DOT_FLOAT
#define ADDR_WRITE_DOT_FLOAT               ((uintptr_t)(const void *)am2_write_dot_float)
#undef ADDR_STR_DOT
#define ADDR_STR_DOT                       ((uintptr_t)(const void *)am2_str_dot)
#undef ADDR_STR_BOOL_0
#define ADDR_STR_BOOL_0                    ((uintptr_t)(const void *)am2_bool_display_strings)
#undef ADDR_CRT_EH_MAGIC
#define ADDR_CRT_EH_MAGIC                  ((uintptr_t)(const void *)am2_crt_eh_magic)
#undef ADDR_PALETTE_CYCLE_TAIL
#define ADDR_PALETTE_CYCLE_TAIL            ((uintptr_t)(const void *)am2_palette_cycle_extra)
#undef ADDR_DEBUG_BLAST_FLAG
#define ADDR_DEBUG_BLAST_FLAG              ((uintptr_t)(const void *)am2_debug_blast_flag)
#undef AM2_SEQ_VARIANT_STRIDE
#define AM2_SEQ_VARIANT_STRIDE             ((uintptr_t)(const void *)am2_seq_variant_stride)
#undef ADDR_CRT_ONE
#define ADDR_CRT_ONE                       ((uintptr_t)(const void *)am2_crt_one)
#undef ADDR_LOGPALETTE_HDR_SMALL
#define ADDR_LOGPALETTE_HDR_SMALL          ((uintptr_t)(const void *)am2_logpalette_hdr_small)

/* Code-read blob strings folded to C literals (CRT names, error banners, the
 * multiplayer status lines, team-win names, bad-map messages). Generated by
 * tools/foldstrings.py; the redirects must come after orig.h's ADDR defines,
 * so this is last. deadstrings.py drops the blob copies. */
#include "foldstr.h"

/* .bss working-state globals reached by a scaled index: redirect their ADDR to
 * the am2_origstate member so the access is symbol-relative ([idx*scale + &sym])
 * -- the original's form -- rather than the constant-base fold MSVC emits for a
 * numeric address ([idx*scale + ADDR/scale in the index]).  See src/vc6/relink.py.
 * am2_origstate reserves the whole region, so the member address is its VA. */
#undef ADDR_ALLY_MATRIX
#define ADDR_ALLY_MATRIX ((uintptr_t)(const void *)am2_origstate.ally_matrix)
#undef ADDR_PADS
#define ADDR_PADS ((uintptr_t)(const void *)am2_origstate.pads)
#undef ADDR_PAD_NUMBERS
#define ADDR_PAD_NUMBERS ((uintptr_t)(const void *)am2_origstate.pad_numbers)
#undef ADDR_MENU_SPRITES
#define ADDR_MENU_SPRITES ((uintptr_t)(const void *)am2_origstate.menu_sprites)
#undef ADDR_ARMY_OBJ_LISTS
#define ADDR_ARMY_OBJ_LISTS ((uintptr_t)(const void *)am2_origstate.army_obj_lists)

/* Bulk .bss data-global redirects (am2_origstate members), so their
 * AM2_IMAGE sites -- converted to (uintptr_t)ADDR below -- are symbol-relative. */
#undef ADDR_AIM_DEADLINE_A
#define ADDR_AIM_DEADLINE_A ((uintptr_t)(const void *)am2_origstate.aim_deadline_a)
#undef ADDR_AIM_DEADLINE_B
#define ADDR_AIM_DEADLINE_B ((uintptr_t)(const void *)am2_origstate.aim_deadline_b)
#undef ADDR_AIM_LIVE_A
#define ADDR_AIM_LIVE_A ((uintptr_t)(const void *)am2_origstate.aim_live_a)
#undef ADDR_AIM_LIVE_B
#define ADDR_AIM_LIVE_B ((uintptr_t)(const void *)am2_origstate.aim_live_b)
#undef ADDR_AIM_POINT_A
#define ADDR_AIM_POINT_A ((uintptr_t)(const void *)am2_origstate.aim_point_a)
#undef ADDR_AIM_POINT_B
#define ADDR_AIM_POINT_B ((uintptr_t)(const void *)am2_origstate.aim_point_b)
#undef ADDR_AIM_STAMP_A
#define ADDR_AIM_STAMP_A ((uintptr_t)(const void *)am2_origstate.aim_stamp_a)
#undef ADDR_AIM_STAMP_B
#define ADDR_AIM_STAMP_B ((uintptr_t)(const void *)am2_origstate.aim_stamp_b)
#undef ADDR_AIR_SAVE_BLOCK
#define ADDR_AIR_SAVE_BLOCK ((uintptr_t)(const void *)am2_origstate.air_save_block)
#undef ADDR_ARMY_PACKET
#define ADDR_ARMY_PACKET ((uintptr_t)(const void *)am2_origstate.army_packet)
#undef ADDR_ARMY_PACKET_LEN
#define ADDR_ARMY_PACKET_LEN ((uintptr_t)(const void *)am2_origstate.army_packet_len)
#undef ADDR_ARMY_PACKET_SEQ
#define ADDR_ARMY_PACKET_SEQ ((uintptr_t)(const void *)am2_origstate.army_packet_seq)
#undef ADDR_CD_PATH
#define ADDR_CD_PATH ((uintptr_t)(const void *)am2_origstate.cd_path)
#undef ADDR_CD_PRESENT
#define ADDR_CD_PRESENT ((uintptr_t)(const void *)am2_origstate.cd_present)
#undef ADDR_COMM_GLOBAL
#define ADDR_COMM_GLOBAL ((uintptr_t)(const void *)am2_origstate.comm_global)
#undef ADDR_CURRENT_BITMAP
#define ADDR_CURRENT_BITMAP ((uintptr_t)(const void *)am2_origstate.current_bitmap)
#undef ADDR_CURRENT_OBJ_SCRIPT
#define ADDR_CURRENT_OBJ_SCRIPT ((uintptr_t)(const void *)am2_origstate.current_obj_script)
#undef ADDR_DEFAULT_OWNER
#define ADDR_DEFAULT_OWNER ((uintptr_t)(const void *)am2_origstate.default_owner)
#undef ADDR_DEF_LINKS
#define ADDR_DEF_LINKS ((uintptr_t)(const void *)am2_origstate.def_links)
#undef ADDR_DEF_LINK_CAP
#define ADDR_DEF_LINK_CAP ((uintptr_t)(const void *)am2_origstate.def_link_cap)
#undef ADDR_DEF_LINK_COUNT
#define ADDR_DEF_LINK_COUNT ((uintptr_t)(const void *)am2_origstate.def_link_count)
#undef ADDR_DEF_OBJ_RECS
#define ADDR_DEF_OBJ_RECS ((uintptr_t)(const void *)am2_origstate.def_obj_recs)
#undef ADDR_DEF_OBJ_REC_CAP
#define ADDR_DEF_OBJ_REC_CAP ((uintptr_t)(const void *)am2_origstate.def_obj_rec_cap)
#undef ADDR_DEF_OBJ_REC_COUNT
#define ADDR_DEF_OBJ_REC_COUNT ((uintptr_t)(const void *)am2_origstate.def_obj_rec_count)
#undef ADDR_DEF_TROOPER_CAP
#define ADDR_DEF_TROOPER_CAP ((uintptr_t)(const void *)am2_origstate.def_trooper_cap)
#undef ADDR_DEF_TROOPER_COUNT
#define ADDR_DEF_TROOPER_COUNT ((uintptr_t)(const void *)am2_origstate.def_trooper_count)
#undef ADDR_DEF_TROOPER_RECS
#define ADDR_DEF_TROOPER_RECS ((uintptr_t)(const void *)am2_origstate.def_trooper_recs)
#undef ADDR_DIFFICULTY
#define ADDR_DIFFICULTY ((uintptr_t)(const void *)am2_origstate.difficulty)
#undef ADDR_DIRTY_HEAD
#define ADDR_DIRTY_HEAD ((uintptr_t)(const void *)am2_origstate.dirty_head)
#undef ADDR_DIRTY_PREV_HEAD
#define ADDR_DIRTY_PREV_HEAD ((uintptr_t)(const void *)am2_origstate.dirty_prev_head)
#undef ADDR_DIRTY_RECTS
#define ADDR_DIRTY_RECTS ((uintptr_t)(const void *)am2_origstate.dirty_rects)
#undef ADDR_DIRTY_TAIL
#define ADDR_DIRTY_TAIL ((uintptr_t)(const void *)am2_origstate.dirty_tail)
#undef ADDR_DIR_SCRATCH
#define ADDR_DIR_SCRATCH ((uintptr_t)(const void *)am2_origstate.dir_scratch)
#undef ADDR_EVENT_BLOCK
#define ADDR_EVENT_BLOCK ((uintptr_t)(const void *)am2_origstate.event_block)
#undef ADDR_EVENT_TABLE
#define ADDR_EVENT_TABLE ((uintptr_t)(const void *)am2_origstate.event_table)
#undef ADDR_EVT_ID15_FLAG
#define ADDR_EVT_ID15_FLAG ((uintptr_t)(const void *)am2_origstate.evt_id15_flag)
#undef ADDR_EVT_ID15_UID
#define ADDR_EVT_ID15_UID ((uintptr_t)(const void *)am2_origstate.evt_id15_uid)
#undef ADDR_EXIT_GAME_FLAG
#define ADDR_EXIT_GAME_FLAG ((uintptr_t)(const void *)am2_origstate.exit_game_flag)
#undef ADDR_FAST_MACHINE
#define ADDR_FAST_MACHINE ((uintptr_t)(const void *)am2_origstate.fast_machine)
#undef ADDR_FULL_REDRAW
#define ADDR_FULL_REDRAW ((uintptr_t)(const void *)am2_origstate.full_redraw)
#undef ADDR_GAMEPROC_BLOCK
#define ADDR_GAMEPROC_BLOCK ((uintptr_t)(const void *)am2_origstate.gameproc_block)
#undef ADDR_GAMEPROC_STR_B
#define ADDR_GAMEPROC_STR_B ((uintptr_t)(const void *)am2_origstate.gameproc_str_b)
#undef ADDR_GAME_CLOCK_MS
#define ADDR_GAME_CLOCK_MS ((uintptr_t)(const void *)am2_origstate.game_clock_ms)
#undef ADDR_GAME_DIR
#define ADDR_GAME_DIR ((uintptr_t)(const void *)am2_origstate.game_dir)
#undef ADDR_HOST_MASK_A
#define ADDR_HOST_MASK_A ((uintptr_t)(const void *)am2_origstate.host_mask_a)
#undef ADDR_HOST_MASK_B
#define ADDR_HOST_MASK_B ((uintptr_t)(const void *)am2_origstate.host_mask_b)
#undef ADDR_HOST_VALUE_3E8
#define ADDR_HOST_VALUE_3E8 ((uintptr_t)(const void *)am2_origstate.host_value_3e8)
#undef ADDR_HWND
#define ADDR_HWND ((uintptr_t)(const void *)am2_origstate.hwnd)
#undef ADDR_LEVEL_INDEX
#define ADDR_LEVEL_INDEX ((uintptr_t)(const void *)am2_origstate.level_index)
#undef ADDR_LEVEL_SOUND_NAME
#define ADDR_LEVEL_SOUND_NAME ((uintptr_t)(const void *)am2_origstate.level_sound_name)
#undef ADDR_LEVEL_STR_A
#define ADDR_LEVEL_STR_A ((uintptr_t)(const void *)am2_origstate.level_str_a)
#undef ADDR_LEVEL_STR_B
#define ADDR_LEVEL_STR_B ((uintptr_t)(const void *)am2_origstate.level_str_b)
#undef ADDR_LEVEL_STR_C
#define ADDR_LEVEL_STR_C ((uintptr_t)(const void *)am2_origstate.level_str_c)
#undef ADDR_LEVEL_STR_D
#define ADDR_LEVEL_STR_D ((uintptr_t)(const void *)am2_origstate.level_str_d)
#undef ADDR_LEVEL_TABLE
#define ADDR_LEVEL_TABLE ((uintptr_t)(const void *)am2_origstate.level_table)
#undef ADDR_LEVEL_TABLE_CAP
#define ADDR_LEVEL_TABLE_CAP ((uintptr_t)(const void *)am2_origstate.level_table_cap)
#undef ADDR_LEVEL_TABLE_COUNT
#define ADDR_LEVEL_TABLE_COUNT ((uintptr_t)(const void *)am2_origstate.level_table_count)
#undef ADDR_MAP_BLOCK
#define ADDR_MAP_BLOCK ((uintptr_t)(const void *)am2_origstate.map_block)
#undef ADDR_MAP_BOUNDS_BOTTOM
#define ADDR_MAP_BOUNDS_BOTTOM ((uintptr_t)(const void *)am2_origstate.map_bounds_bottom)
#undef ADDR_MAP_BOUNDS_LEFT
#define ADDR_MAP_BOUNDS_LEFT ((uintptr_t)(const void *)am2_origstate.map_bounds_left)
#undef ADDR_MAP_BOUNDS_RIGHT
#define ADDR_MAP_BOUNDS_RIGHT ((uintptr_t)(const void *)am2_origstate.map_bounds_right)
#undef ADDR_MAP_BOUNDS_TOP
#define ADDR_MAP_BOUNDS_TOP ((uintptr_t)(const void *)am2_origstate.map_bounds_top)
#undef ADDR_MAP_DESC
#define ADDR_MAP_DESC ((uintptr_t)(const void *)am2_origstate.map_desc)
#undef ADDR_MAP_FOLDER
#define ADDR_MAP_FOLDER ((uintptr_t)(const void *)am2_origstate.map_folder)
#undef ADDR_MAP_NAME
#define ADDR_MAP_NAME ((uintptr_t)(const void *)am2_origstate.map_name)
#undef ADDR_MAP_PADBIT_LAYER
#define ADDR_MAP_PADBIT_LAYER ((uintptr_t)(const void *)am2_origstate.map_padbit_layer)
#undef ADDR_MAP_PAD_LAYER
#define ADDR_MAP_PAD_LAYER ((uintptr_t)(const void *)am2_origstate.map_pad_layer)
#undef ADDR_MAP_TILES_H
#define ADDR_MAP_TILES_H ((uintptr_t)(const void *)am2_origstate.map_tiles_h)
#undef ADDR_MAP_TILES_W
#define ADDR_MAP_TILES_W ((uintptr_t)(const void *)am2_origstate.map_tiles_w)
#undef ADDR_MENU_MODE
#define ADDR_MENU_MODE ((uintptr_t)(const void *)am2_origstate.menu_mode)
#undef ADDR_MESSAGE_BMP_NAME
#define ADDR_MESSAGE_BMP_NAME ((uintptr_t)(const void *)am2_origstate.message_bmp_name)
#undef ADDR_MISSILE_DEFS
#define ADDR_MISSILE_DEFS ((uintptr_t)(const void *)am2_origstate.missile_defs)
#undef ADDR_MOVIE_COUNT
#define ADDR_MOVIE_COUNT ((uintptr_t)(const void *)am2_origstate.movie_count)
#undef ADDR_MP_SCRIPT_NAME
#define ADDR_MP_SCRIPT_NAME ((uintptr_t)(const void *)am2_origstate.mp_script_name)
#undef ADDR_MP_SESSION
#define ADDR_MP_SESSION ((uintptr_t)(const void *)am2_origstate.mp_session)
#undef ADDR_MSG_CHAT
#define ADDR_MSG_CHAT ((uintptr_t)(const void *)am2_origstate.msg_chat)
#undef ADDR_MSG_GAME_PAUSE
#define ADDR_MSG_GAME_PAUSE ((uintptr_t)(const void *)am2_origstate.msg_game_pause)
#undef ADDR_NAME_TABLE_BASE
#define ADDR_NAME_TABLE_BASE ((uintptr_t)(const void *)am2_origstate.name_table_base)
#undef ADDR_NAME_TABLE_CAP
#define ADDR_NAME_TABLE_CAP ((uintptr_t)(const void *)am2_origstate.name_table_cap)
#undef ADDR_NAME_TABLE_COUNT
#define ADDR_NAME_TABLE_COUNT ((uintptr_t)(const void *)am2_origstate.name_table_count)
#undef ADDR_NEXT_UID
#define ADDR_NEXT_UID ((uintptr_t)(const void *)am2_origstate.next_uid)
#undef ADDR_OBJ_CTX_OBJ
#define ADDR_OBJ_CTX_OBJ ((uintptr_t)(const void *)am2_origstate.obj_ctx_obj)
#undef ADDR_OBJ_CTX_OBJ_PREV
#define ADDR_OBJ_CTX_OBJ_PREV ((uintptr_t)(const void *)am2_origstate.obj_ctx_obj_prev)
#undef ADDR_OBJ_CTX_SET
#define ADDR_OBJ_CTX_SET ((uintptr_t)(const void *)am2_origstate.obj_ctx_set)
#undef ADDR_OBJ_CTX_SET_PREV
#define ADDR_OBJ_CTX_SET_PREV ((uintptr_t)(const void *)am2_origstate.obj_ctx_set_prev)
#undef ADDR_OBJ_CTX_VAL
#define ADDR_OBJ_CTX_VAL ((uintptr_t)(const void *)am2_origstate.obj_ctx_val)
#undef ADDR_OBJ_CTX_VAL_PREV
#define ADDR_OBJ_CTX_VAL_PREV ((uintptr_t)(const void *)am2_origstate.obj_ctx_val_prev)
#undef ADDR_OBJ_MAP_DESC
#define ADDR_OBJ_MAP_DESC ((uintptr_t)(const void *)am2_origstate.obj_map_desc)
#undef ADDR_OBJ_SCRIPTS
#define ADDR_OBJ_SCRIPTS ((uintptr_t)(const void *)am2_origstate.obj_scripts)
#undef ADDR_OBJ_SCRIPT_CAP
#define ADDR_OBJ_SCRIPT_CAP ((uintptr_t)(const void *)am2_origstate.obj_script_cap)
#undef ADDR_OBJ_TABLE_RECORDS
#define ADDR_OBJ_TABLE_RECORDS ((uintptr_t)(const void *)am2_origstate.obj_table_records)
#undef ADDR_OPT_BIG_MOVIES
#define ADDR_OPT_BIG_MOVIES ((uintptr_t)(const void *)am2_origstate.opt_big_movies)
#undef ADDR_OPT_TRACE_PF
#define ADDR_OPT_TRACE_PF ((uintptr_t)(const void *)am2_origstate.opt_trace_pf)
#undef ADDR_OVERLAY_DIRTY
#define ADDR_OVERLAY_DIRTY ((uintptr_t)(const void *)am2_origstate.overlay_dirty)
#undef ADDR_PAD_COUNT
#define ADDR_PAD_COUNT ((uintptr_t)(const void *)am2_origstate.pad_count)
#undef ADDR_PLACEMENTS
#define ADDR_PLACEMENTS ((uintptr_t)(const void *)am2_origstate.placements)
#undef ADDR_PLACEMENT_CAP
#define ADDR_PLACEMENT_CAP ((uintptr_t)(const void *)am2_origstate.placement_cap)
#undef ADDR_PLACEMENT_COUNT
#define ADDR_PLACEMENT_COUNT ((uintptr_t)(const void *)am2_origstate.placement_count)
#undef ADDR_REGIONS
#define ADDR_REGIONS ((uintptr_t)(const void *)am2_origstate.regions)
#undef ADDR_REGION_COST
#define ADDR_REGION_COST ((uintptr_t)(const void *)am2_origstate.region_cost)
#undef ADDR_REGION_NEXT
#define ADDR_REGION_NEXT ((uintptr_t)(const void *)am2_origstate.region_next)
#undef ADDR_REGION_OF_CELL
#define ADDR_REGION_OF_CELL ((uintptr_t)(const void *)am2_origstate.region_of_cell)
#undef ADDR_REGION_STAMP
#define ADDR_REGION_STAMP ((uintptr_t)(const void *)am2_origstate.region_stamp)
#undef ADDR_REGION_STRIDE
#define ADDR_REGION_STRIDE ((uintptr_t)(const void *)am2_origstate.region_stride)
#undef ADDR_ROACH_MASK
#define ADDR_ROACH_MASK ((uintptr_t)(const void *)am2_origstate.roach_mask)
#undef ADDR_ROACH_MASK_COUNT
#define ADDR_ROACH_MASK_COUNT ((uintptr_t)(const void *)am2_origstate.roach_mask_count)
#undef ADDR_ROW_LUT_DOUBLES
#define ADDR_ROW_LUT_DOUBLES ((uintptr_t)(const void *)am2_origstate.row_lut_doubles)
#undef ADDR_SAVED_BATTLE_NAME
#define ADDR_SAVED_BATTLE_NAME ((uintptr_t)(const void *)am2_origstate.saved_battle_name)
#undef ADDR_SAVED_PLAYER_NAME
#define ADDR_SAVED_PLAYER_NAME ((uintptr_t)(const void *)am2_origstate.saved_player_name)
#undef ADDR_SCORE_LIMIT
#define ADDR_SCORE_LIMIT ((uintptr_t)(const void *)am2_origstate.score_limit)
#undef ADDR_SCRIPT_CONDITIONS
#define ADDR_SCRIPT_CONDITIONS ((uintptr_t)(const void *)am2_origstate.script_conditions)
#undef ADDR_SCRIPT_CONTEXT
#define ADDR_SCRIPT_CONTEXT ((uintptr_t)(const void *)am2_origstate.script_context)
#undef ADDR_SCRIPT_NAMES
#define ADDR_SCRIPT_NAMES ((uintptr_t)(const void *)am2_origstate.script_names)
#undef ADDR_SCRIPT_NAME_CAP
#define ADDR_SCRIPT_NAME_CAP ((uintptr_t)(const void *)am2_origstate.script_name_cap)
#undef ADDR_SCRIPT_NAME_COUNT
#define ADDR_SCRIPT_NAME_COUNT ((uintptr_t)(const void *)am2_origstate.script_name_count)
#undef ADDR_SCRIPT_STATE_FLAG
#define ADDR_SCRIPT_STATE_FLAG ((uintptr_t)(const void *)am2_origstate.script_state_flag)
#undef ADDR_SCRIPT_WORD_BUF
#define ADDR_SCRIPT_WORD_BUF ((uintptr_t)(const void *)am2_origstate.script_word_buf)
#undef ADDR_SLOT_HEADINGS
#define ADDR_SLOT_HEADINGS ((uintptr_t)(const void *)am2_origstate.slot_headings)
#undef ADDR_SLOT_IS_VEHICLE
#define ADDR_SLOT_IS_VEHICLE ((uintptr_t)(const void *)am2_origstate.slot_is_vehicle)
#undef ADDR_SLOT_POSITIONS
#define ADDR_SLOT_POSITIONS ((uintptr_t)(const void *)am2_origstate.slot_positions)
#undef ADDR_SLOW_MACHINE
#define ADDR_SLOW_MACHINE ((uintptr_t)(const void *)am2_origstate.slow_machine)
#undef ADDR_SPRITE_SET_SHARED
#define ADDR_SPRITE_SET_SHARED ((uintptr_t)(const void *)am2_origstate.sprite_set_shared)
#undef ADDR_SPRITE_SET_THIRD
#define ADDR_SPRITE_SET_THIRD ((uintptr_t)(const void *)am2_origstate.sprite_set_third)
#undef ADDR_SPRITE_SET_TITLE
#define ADDR_SPRITE_SET_TITLE ((uintptr_t)(const void *)am2_origstate.sprite_set_title)
#undef ADDR_STREAM_VOLUME
#define ADDR_STREAM_VOLUME ((uintptr_t)(const void *)am2_origstate.stream_volume)
#undef ADDR_SVAR_BLUE
#define ADDR_SVAR_BLUE ((uintptr_t)(const void *)am2_origstate.svar_blue)
#undef ADDR_SVAR_BLUESCORE
#define ADDR_SVAR_BLUESCORE ((uintptr_t)(const void *)am2_origstate.svar_bluescore)
#undef ADDR_SVAR_DIFFICULTY
#define ADDR_SVAR_DIFFICULTY ((uintptr_t)(const void *)am2_origstate.svar_difficulty)
#undef ADDR_SVAR_GREEN
#define ADDR_SVAR_GREEN ((uintptr_t)(const void *)am2_origstate.svar_green)
#undef ADDR_SVAR_GREENSCORE
#define ADDR_SVAR_GREENSCORE ((uintptr_t)(const void *)am2_origstate.svar_greenscore)
#undef ADDR_SVAR_GREY
#define ADDR_SVAR_GREY ((uintptr_t)(const void *)am2_origstate.svar_grey)
#undef ADDR_SVAR_GREYSCORE
#define ADDR_SVAR_GREYSCORE ((uintptr_t)(const void *)am2_origstate.svar_greyscore)
#undef ADDR_SVAR_ID15
#define ADDR_SVAR_ID15 ((uintptr_t)(const void *)am2_origstate.svar_id15)
#undef ADDR_SVAR_ME
#define ADDR_SVAR_ME ((uintptr_t)(const void *)am2_origstate.svar_me)
#undef ADDR_SVAR_NUMGREEN
#define ADDR_SVAR_NUMGREEN ((uintptr_t)(const void *)am2_origstate.svar_numgreen)
#undef ADDR_SVAR_SYSTEMSPEED
#define ADDR_SVAR_SYSTEMSPEED ((uintptr_t)(const void *)am2_origstate.svar_systemspeed)
#undef ADDR_SVAR_TAN
#define ADDR_SVAR_TAN ((uintptr_t)(const void *)am2_origstate.svar_tan)
#undef ADDR_SVAR_TANSCORE
#define ADDR_SVAR_TANSCORE ((uintptr_t)(const void *)am2_origstate.svar_tanscore)
#undef ADDR_TILESET_RESERVE
#define ADDR_TILESET_RESERVE ((uintptr_t)(const void *)am2_origstate.tileset_reserve)
#undef ADDR_TILE_LINE_BUF
#define ADDR_TILE_LINE_BUF ((uintptr_t)(const void *)am2_origstate.tile_line_buf)
#undef ADDR_TIMER_COUNT
#define ADDR_TIMER_COUNT ((uintptr_t)(const void *)am2_origstate.timer_count)
#undef ADDR_TIMER_TABLE
#define ADDR_TIMER_TABLE ((uintptr_t)(const void *)am2_origstate.timer_table)
#undef ADDR_TRIG_ATAN_COS
#define ADDR_TRIG_ATAN_COS ((uintptr_t)(const void *)am2_origstate.trig_atan_cos)
#undef ADDR_TRIG_ATAN_SIN
#define ADDR_TRIG_ATAN_SIN ((uintptr_t)(const void *)am2_origstate.trig_atan_sin)
#undef ADDR_TRIG_COS
#define ADDR_TRIG_COS ((uintptr_t)(const void *)am2_origstate.trig_cos)
#undef ADDR_TRIG_SIN
#define ADDR_TRIG_SIN ((uintptr_t)(const void *)am2_origstate.trig_sin)
#undef ADDR_TURRET_ANIMS
#define ADDR_TURRET_ANIMS ((uintptr_t)(const void *)am2_origstate.turret_anims)
#undef ADDR_VEHICLE_ANIMS
#define ADDR_VEHICLE_ANIMS ((uintptr_t)(const void *)am2_origstate.vehicle_anims)
#undef ADDR_VEHICLE_MASK
#define ADDR_VEHICLE_MASK ((uintptr_t)(const void *)am2_origstate.vehicle_mask)
#undef ADDR_VEHICLE_MASK_COUNT
#define ADDR_VEHICLE_MASK_COUNT ((uintptr_t)(const void *)am2_origstate.vehicle_mask_count)
#undef ADDR_VIEW_RECT_PREV
#define ADDR_VIEW_RECT_PREV ((uintptr_t)(const void *)am2_origstate.view_rect_prev)
#undef ADDR_VOLUME_AT_ZERO
#define ADDR_VOLUME_AT_ZERO ((uintptr_t)(const void *)am2_origstate.volume_at_zero)
#undef ADDR_VOLUME_VOICE
#define ADDR_VOLUME_VOICE ((uintptr_t)(const void *)am2_origstate.volume_voice)
#undef ADDR_ZERO_POINT
#define ADDR_ZERO_POINT ((uintptr_t)(const void *)am2_origstate.zero_point)

/* Every remaining .bss ADDR the game uses, redirected to its am2_origstate
 * member (matched by exact VA; each member already existed).  Without these
 * the reconstruction reached them by raw number, which only worked while the
 * struct sat at 0x0048E000 -- the VC6 LINK.EXE image places it elsewhere and
 * ApplyGameSettings walked ADDR_ARMY_POINTS (raw) up to ADDR_SCORE_LIMIT
 * (migrated), straight off the end of .bss.  `&member` so scalar and array
 * members alike yield their address. */
#undef ADDR_PACKET_BUFFERS_END
#define ADDR_PACKET_BUFFERS_END ((uintptr_t)(const void *)&am2_origstate.packet_buffers_end)
#undef ADDR_PLAYER_RECORDS
#define ADDR_PLAYER_RECORDS ((uintptr_t)(const void *)&am2_origstate.player_records)
#undef ADDR_DEFAULT_PLAYER_EVT
#define ADDR_DEFAULT_PLAYER_EVT ((uintptr_t)(const void *)&am2_origstate.default_player_evt)
#undef ADDR_PACKET_EVENT_B2
#define ADDR_PACKET_EVENT_B2 ((uintptr_t)(const void *)&am2_origstate.default_player_evt)
#undef ADDR_MSG_LIST_B
#define ADDR_MSG_LIST_B ((uintptr_t)(const void *)&am2_origstate.msg_list_b)
#undef ADDR_PACKET_THREAD
#define ADDR_PACKET_THREAD ((uintptr_t)(const void *)&am2_origstate.packet_thread)
#undef ADDR_PLAYER_SLOT_MASK
#define ADDR_PLAYER_SLOT_MASK ((uintptr_t)(const void *)&am2_origstate.player_slot_mask)
#undef ADDR_COMM_NO_BUFFERS_LATCH
#define ADDR_COMM_NO_BUFFERS_LATCH ((uintptr_t)(const void *)&am2_origstate.comm_no_buffers_latch)
#undef ADDR_LAST_MSG_VALUE
#define ADDR_LAST_MSG_VALUE ((uintptr_t)(const void *)&am2_origstate.last_msg_value)
#undef ADDR_LAST_MSG_CHECKSUM
#define ADDR_LAST_MSG_CHECKSUM ((uintptr_t)(const void *)&am2_origstate.last_msg_checksum)
#undef ADDR_PACKET_RECORDS
#define ADDR_PACKET_RECORDS ((uintptr_t)(const void *)&am2_origstate.packet_records)
#undef ADDR_PACKET_STATE
#define ADDR_PACKET_STATE ((uintptr_t)(const void *)&am2_origstate.packet_state)
#undef ADDR_MSG_LIST_DELAYED
#define ADDR_MSG_LIST_DELAYED ((uintptr_t)(const void *)&am2_origstate.msg_list_delayed)
#undef ADDR_RECV_SCRATCH
#define ADDR_RECV_SCRATCH ((uintptr_t)(const void *)&am2_origstate.recv_scratch)
#undef ADDR_PACKET_THREAD_ID
#define ADDR_PACKET_THREAD_ID ((uintptr_t)(const void *)&am2_origstate.packet_thread_id)
#undef ADDR_MSG_WANTED_FLAGS
#define ADDR_MSG_WANTED_FLAGS ((uintptr_t)(const void *)&am2_origstate.msg_wanted_flags)
#undef ADDR_SEND_ROLL
#define ADDR_SEND_ROLL ((uintptr_t)(const void *)&am2_origstate.send_roll)
#undef ADDR_SIGHT_BLOCK_BY_DIR
#define ADDR_SIGHT_BLOCK_BY_DIR ((uintptr_t)(const void *)&am2_origstate.sight_block_by_dir)
#undef ADDR_PERFRAME_COUNT_A
#define ADDR_PERFRAME_COUNT_A ((uintptr_t)(const void *)&am2_origstate.perframe_count_a)
#undef ADDR_PERFRAME_COUNT_B
#define ADDR_PERFRAME_COUNT_B ((uintptr_t)(const void *)&am2_origstate.perframe_count_b)
#undef ADDR_SIGHT_GENERATION
#define ADDR_SIGHT_GENERATION ((uintptr_t)(const void *)&am2_origstate.sight_generation)
#undef ADDR_AIR_LEG1_DY
#define ADDR_AIR_LEG1_DY ((uintptr_t)(const void *)&am2_origstate.air_leg1_dy)
#undef ADDR_AIR_LEG2_DIVISOR
#define ADDR_AIR_LEG2_DIVISOR ((uintptr_t)(const void *)&am2_origstate.air_leg2_divisor)
#undef ADDR_AIR_LEG2_MS_SPAN
#define ADDR_AIR_LEG2_MS_SPAN ((uintptr_t)(const void *)&am2_origstate.air_leg2_ms_span)
#undef ADDR_AIR_LEG3_DX
#define ADDR_AIR_LEG3_DX ((uintptr_t)(const void *)&am2_origstate.air_leg3_dx)
#undef ADDR_AIR_SPRITES_2
#define ADDR_AIR_SPRITES_2 ((uintptr_t)(const void *)&am2_origstate.air_sprites_2)
#undef ADDR_AIR_SPRITES_3
#define ADDR_AIR_SPRITES_3 ((uintptr_t)(const void *)&am2_origstate.air_sprites_3)
#undef ADDR_AIR_SPRITES_6
#define ADDR_AIR_SPRITES_6 ((uintptr_t)(const void *)&am2_origstate.air_sprites_6)
#undef ADDR_AIR_SPRITES_EDGE
#define ADDR_AIR_SPRITES_EDGE ((uintptr_t)(const void *)&am2_origstate.air_sprites_edge)
#undef ADDR_AIR_LEG2_DY
#define ADDR_AIR_LEG2_DY ((uintptr_t)(const void *)&am2_origstate.air_leg2_dy)
#undef ADDR_AIR_LEG3_DY
#define ADDR_AIR_LEG3_DY ((uintptr_t)(const void *)&am2_origstate.air_leg3_dy)
#undef ADDR_AIR_LEG1_X0
#define ADDR_AIR_LEG1_X0 ((uintptr_t)(const void *)&am2_origstate.air_leg1_x0)
#undef ADDR_AIR_LEG1_DX
#define ADDR_AIR_LEG1_DX ((uintptr_t)(const void *)&am2_origstate.air_leg1_dx)
#undef ADDR_AIR_LEG3_X1
#define ADDR_AIR_LEG3_X1 ((uintptr_t)(const void *)&am2_origstate.air_leg3_x1)
#undef ADDR_SPRITE_LIST_CAP
#define ADDR_SPRITE_LIST_CAP ((uintptr_t)(const void *)&am2_origstate.sprite_list_cap)
#undef ADDR_SPRITE_LIST_COUNT
#define ADDR_SPRITE_LIST_COUNT ((uintptr_t)(const void *)&am2_origstate.sprite_list_count)
#undef ADDR_SPRITE_LIST
#define ADDR_SPRITE_LIST ((uintptr_t)(const void *)&am2_origstate.sprite_list)
#undef ADDR_ARMY_RAMP_TABLES
#define ADDR_ARMY_RAMP_TABLES ((uintptr_t)(const void *)&am2_origstate.army_ramp_tables)
#undef ADDR_CHAT_COLOUR_TABLE
#define ADDR_CHAT_COLOUR_TABLE ((uintptr_t)(const void *)&am2_origstate.chat_colour_table)
#undef ADDR_TYPE2_ACTION_LIST
#define ADDR_TYPE2_ACTION_LIST ((uintptr_t)(const void *)&am2_origstate.type2_action_list)
#undef ADDR_WINCPUID_FN
#define ADDR_WINCPUID_FN ((uintptr_t)(const void *)&am2_origstate.wincpuid_fn)
#undef ADDR_LAST_MESSAGE
#define ADDR_LAST_MESSAGE ((uintptr_t)(const void *)&am2_origstate.last_message)
#undef ADDR_CPUNORMSPEED_FN
#define ADDR_CPUNORMSPEED_FN ((uintptr_t)(const void *)&am2_origstate.cpunormspeed_fn)
#undef ADDR_OPT_MAP_NAME
#define ADDR_OPT_MAP_NAME ((uintptr_t)(const void *)&am2_origstate.opt_map_name)
#undef ADDR_APP_ACTIVE
#define ADDR_APP_ACTIVE ((uintptr_t)(const void *)&am2_origstate.app_active)
#undef ADDR_PRESENT_ENABLED
#define ADDR_PRESENT_ENABLED ((uintptr_t)(const void *)&am2_origstate.present_enabled)
#undef ADDR_APP_MUTEX
#define ADDR_APP_MUTEX ((uintptr_t)(const void *)&am2_origstate.app_mutex)
#undef ADDR_OPT_NO_INTRO
#define ADDR_OPT_NO_INTRO ((uintptr_t)(const void *)&am2_origstate.opt_no_intro)
#undef ADDR_SOUND_SLOTS
#define ADDR_SOUND_SLOTS ((uintptr_t)(const void *)&am2_origstate.sound_slots)
#undef ADDR_SOUND_DYNAMIC
#define ADDR_SOUND_DYNAMIC ((uintptr_t)(const void *)&am2_origstate.sound_slots_end)
#undef ADDR_SOUND_SLOTS_END
#define ADDR_SOUND_SLOTS_END ((uintptr_t)(const void *)&am2_origstate.sound_slots_end)
#undef ADDR_SOUND_DYNAMIC_LAST
#define ADDR_SOUND_DYNAMIC_LAST ((uintptr_t)(const void *)&am2_origstate.sound_dynamic_last)
#undef ADDR_AUDIO_BUFFER
#define ADDR_AUDIO_BUFFER ((uintptr_t)(const void *)&am2_origstate.audio_buffer)
#undef ADDR_AUDIO_TIMER_ID
#define ADDR_AUDIO_TIMER_ID ((uintptr_t)(const void *)&am2_origstate.audio_timer_id)
#undef ADDR_AUDIO_WAVEFORMAT
#define ADDR_AUDIO_WAVEFORMAT ((uintptr_t)(const void *)&am2_origstate.audio_waveformat)
#undef ADDR_AUDIO_HMMIO
#define ADDR_AUDIO_HMMIO ((uintptr_t)(const void *)&am2_origstate.audio_hmmio)
#undef ADDR_AUDIO_DATA_CHUNK
#define ADDR_AUDIO_DATA_CHUNK ((uintptr_t)(const void *)&am2_origstate.audio_data_chunk)
#undef ADDR_AUDIO_RIFF_CHUNK
#define ADDR_AUDIO_RIFF_CHUNK ((uintptr_t)(const void *)&am2_origstate.audio_riff_chunk)
#undef ADDR_AUDIO_BUFFER_2
#define ADDR_AUDIO_BUFFER_2 ((uintptr_t)(const void *)&am2_origstate.audio_buffer_2)
#undef ADDR_AUDIO_BUFFER_SIZE
#define ADDR_AUDIO_BUFFER_SIZE ((uintptr_t)(const void *)&am2_origstate.audio_buffer_size)
#undef ADDR_AUDIO_PERIOD
#define ADDR_AUDIO_PERIOD ((uintptr_t)(const void *)&am2_origstate.audio_period)
#undef ADDR_AUDIO_CURSOR_B
#define ADDR_AUDIO_CURSOR_B ((uintptr_t)(const void *)&am2_origstate.audio_cursor_b)
#undef ADDR_AUDIO_CURSOR_A
#define ADDR_AUDIO_CURSOR_A ((uintptr_t)(const void *)&am2_origstate.audio_cursor_a)
#undef ADDR_AUDIO_VALID_BYTES
#define ADDR_AUDIO_VALID_BYTES ((uintptr_t)(const void *)&am2_origstate.audio_valid_bytes)
#undef ADDR_AUDIO_READ_FAILED
#define ADDR_AUDIO_READ_FAILED ((uintptr_t)(const void *)&am2_origstate.audio_read_failed)
#undef ADDR_AUDIO_LOOPING
#define ADDR_AUDIO_LOOPING ((uintptr_t)(const void *)&am2_origstate.audio_looping)
#undef ADDR_AUDIO_AT_END
#define ADDR_AUDIO_AT_END ((uintptr_t)(const void *)&am2_origstate.audio_at_end)
#undef ADDR_AUDIO_TIMER_RUN
#define ADDR_AUDIO_TIMER_RUN ((uintptr_t)(const void *)&am2_origstate.audio_timer_run)
#undef ADDR_AUDIO_ENABLED
#define ADDR_AUDIO_ENABLED ((uintptr_t)(const void *)&am2_origstate.audio_enabled)
#undef ADDR_DSOUND
#define ADDR_DSOUND ((uintptr_t)(const void *)&am2_origstate.dsound)
#undef ADDR_DS_PRIMARY
#define ADDR_DS_PRIMARY ((uintptr_t)(const void *)&am2_origstate.ds_primary)
#undef ADDR_DS_LISTENER
#define ADDR_DS_LISTENER ((uintptr_t)(const void *)&am2_origstate.ds_listener)
#undef ADDR_AUDIO_IN_CALLBACK
#define ADDR_AUDIO_IN_CALLBACK ((uintptr_t)(const void *)&am2_origstate.audio_in_callback)
#undef ADDR_CONNECTION_LIST
#define ADDR_CONNECTION_LIST ((uintptr_t)(const void *)&am2_origstate.connection_list)
#undef ADDR_OUR_SLOT
#define ADDR_OUR_SLOT ((uintptr_t)(const void *)&am2_origstate.our_slot)
#undef ADDR_SESSION_LIST
#define ADDR_SESSION_LIST ((uintptr_t)(const void *)&am2_origstate.session_list)
#undef ADDR_MSG_READY_TO_LOAD
#define ADDR_MSG_READY_TO_LOAD ((uintptr_t)(const void *)&am2_origstate.msg_ready_to_load)
#undef ADDR_MSG_GAME_READY
#define ADDR_MSG_GAME_READY ((uintptr_t)(const void *)&am2_origstate.msg_game_ready)
#undef ADDR_RESEND_BUF
#define ADDR_RESEND_BUF ((uintptr_t)(const void *)&am2_origstate.resend_buf)
#undef ADDR_MSG_MAP
#define ADDR_MSG_MAP ((uintptr_t)(const void *)&am2_origstate.msg_map)
#undef ADDR_RESEND_SCRATCH
#define ADDR_RESEND_SCRATCH ((uintptr_t)(const void *)&am2_origstate.resend_scratch)
#undef ADDR_MSG_END_SETUP
#define ADDR_MSG_END_SETUP ((uintptr_t)(const void *)&am2_origstate.msg_end_setup)
#undef ADDR_PLAYER_MSG
#define ADDR_PLAYER_MSG ((uintptr_t)(const void *)&am2_origstate.player_msg)
#undef ADDR_MSG_GAME_START
#define ADDR_MSG_GAME_START ((uintptr_t)(const void *)&am2_origstate.msg_game_start)
#undef ADDR_GAME_SEED_SENT
#define ADDR_GAME_SEED_SENT ((uintptr_t)(const void *)&am2_origstate.game_seed_sent)
#undef ADDR_MSG_COLOR
#define ADDR_MSG_COLOR ((uintptr_t)(const void *)&am2_origstate.msg_color)
#undef ADDR_MSG_TEAM
#define ADDR_MSG_TEAM ((uintptr_t)(const void *)&am2_origstate.msg_team)
#undef ADDR_DATA_CHECKSUM
#define ADDR_DATA_CHECKSUM ((uintptr_t)(const void *)&am2_origstate.data_checksum)
#undef ADDR_TIMEOUT_LOGGED_AT
#define ADDR_TIMEOUT_LOGGED_AT ((uintptr_t)(const void *)&am2_origstate.timeout_logged_at)
#undef ADDR_CHANGE_LIMIT_FLOOR
#define ADDR_CHANGE_LIMIT_FLOOR ((uintptr_t)(const void *)&am2_origstate.change_limit_floor)
#undef ADDR_BANDWIDTH_LAST_SEQ
#define ADDR_BANDWIDTH_LAST_SEQ ((uintptr_t)(const void *)&am2_origstate.bandwidth_last_seq)
#undef ADDR_AIM_SPRITES_A
#define ADDR_AIM_SPRITES_A ((uintptr_t)(const void *)&am2_origstate.aim_sprites_a)
#undef ADDR_AIM_SPRITES_B
#define ADDR_AIM_SPRITES_B ((uintptr_t)(const void *)&am2_origstate.aim_sprites_b)
#undef ADDR_AIM_FRAME_B
#define ADDR_AIM_FRAME_B ((uintptr_t)(const void *)&am2_origstate.aim_frame_b)
#undef ADDR_PALETTE_GLYPHS
#define ADDR_PALETTE_GLYPHS ((uintptr_t)(const void *)&am2_origstate.palette_glyphs)
#undef ADDR_MENU_SAVED_RECT
#define ADDR_MENU_SAVED_RECT ((uintptr_t)(const void *)&am2_origstate.menu_saved_rect)
#undef ADDR_MENU_ROW
#define ADDR_MENU_ROW ((uintptr_t)(const void *)&am2_origstate.menu_row)
#undef ADDR_MENU_SPRITES_END
#define ADDR_MENU_SPRITES_END ((uintptr_t)(const void *)&am2_origstate.menu_sprites_end)
#undef ADDR_MENU_OVERLAY_A
#define ADDR_MENU_OVERLAY_A ((uintptr_t)(const void *)&am2_origstate.menu_overlay_a)
#undef ADDR_MENU_OVERLAY_B
#define ADDR_MENU_OVERLAY_B ((uintptr_t)(const void *)&am2_origstate.menu_overlay_b)
#undef ADDR_MENU_INK
#define ADDR_MENU_INK ((uintptr_t)(const void *)&am2_origstate.menu_ink)
#undef ADDR_MENU_OVERLAY_A_INK
#define ADDR_MENU_OVERLAY_A_INK ((uintptr_t)(const void *)&am2_origstate.menu_overlay_a_ink)
#undef ADDR_MENU_OVERLAY_B_INK
#define ADDR_MENU_OVERLAY_B_INK ((uintptr_t)(const void *)&am2_origstate.menu_overlay_b_ink)
#undef ADDR_MENU_CURSOR_DX
#define ADDR_MENU_CURSOR_DX ((uintptr_t)(const void *)&am2_origstate.menu_cursor_dx)
#undef ADDR_MENU_OVERLAY_A_DX
#define ADDR_MENU_OVERLAY_A_DX ((uintptr_t)(const void *)&am2_origstate.menu_overlay_a_dx)
#undef ADDR_MENU_OVERLAY_B_DX
#define ADDR_MENU_OVERLAY_B_DX ((uintptr_t)(const void *)&am2_origstate.menu_overlay_b_dx)
#undef ADDR_MENU_CURSOR_PREV
#define ADDR_MENU_CURSOR_PREV ((uintptr_t)(const void *)&am2_origstate.menu_cursor_prev)
#undef ADDR_MENU_CURSOR_RECT
#define ADDR_MENU_CURSOR_RECT ((uintptr_t)(const void *)&am2_origstate.menu_cursor_rect)
#undef ADDR_MENU_ANIM_NEXT
#define ADDR_MENU_ANIM_NEXT ((uintptr_t)(const void *)&am2_origstate.menu_anim_next)
#undef ADDR_MENU_ANIM_FRAME
#define ADDR_MENU_ANIM_FRAME ((uintptr_t)(const void *)&am2_origstate.menu_anim_frame)
#undef ADDR_MENU_ROW_STAMP
#define ADDR_MENU_ROW_STAMP ((uintptr_t)(const void *)&am2_origstate.menu_row_stamp)
#undef ADDR_MENU_SURFACE
#define ADDR_MENU_SURFACE ((uintptr_t)(const void *)&am2_origstate.menu_surface)
#undef ADDR_FLAME_RECORD
#define ADDR_FLAME_RECORD ((uintptr_t)(const void *)&am2_origstate.flame_record)
#undef ADDR_MENU_ENABLED
#define ADDR_MENU_ENABLED ((uintptr_t)(const void *)&am2_origstate.menu_enabled)
#undef ADDR_MENU_SAVED_VALID
#define ADDR_MENU_SAVED_VALID ((uintptr_t)(const void *)&am2_origstate.menu_saved_valid)
#undef ADDR_HUD_WIDGET_A
#define ADDR_HUD_WIDGET_A ((uintptr_t)(const void *)&am2_origstate.hud_widget_a)
#undef ADDR_HUD_WIDGET_TABLE
#define ADDR_HUD_WIDGET_TABLE ((uintptr_t)(const void *)&am2_origstate.hud_widget_table)
#undef ADDR_HUD_WIDGET_C
#define ADDR_HUD_WIDGET_C ((uintptr_t)(const void *)&am2_origstate.hud_widget_c)
#undef ADDR_HUD_INDEX
#define ADDR_HUD_INDEX ((uintptr_t)(const void *)&am2_origstate.hud_index)
#undef ADDR_HUD_WIDGET_B
#define ADDR_HUD_WIDGET_B ((uintptr_t)(const void *)&am2_origstate.hud_widget_b)
#undef ADDR_VIEW_RECT_ON
#define ADDR_VIEW_RECT_ON ((uintptr_t)(const void *)&am2_origstate.view_rect_on)
#undef ADDR_RADAR_COLOURS
#define ADDR_RADAR_COLOURS ((uintptr_t)(const void *)&am2_origstate.radar_colours)
#undef ADDR_DRAG_ANCHOR
#define ADDR_DRAG_ANCHOR ((uintptr_t)(const void *)&am2_origstate.drag_anchor)
#undef ADDR_VIEW_RECT
#define ADDR_VIEW_RECT ((uintptr_t)(const void *)&am2_origstate.view_rect)
#undef ADDR_POINTER_MODE
#define ADDR_POINTER_MODE ((uintptr_t)(const void *)&am2_origstate.pointer_mode)
#undef ADDR_HUD_DIRTY
#define ADDR_HUD_DIRTY ((uintptr_t)(const void *)&am2_origstate.hud_dirty)
#undef ADDR_PLACE_FLAG_4FCF88
#define ADDR_PLACE_FLAG_4FCF88 ((uintptr_t)(const void *)&am2_origstate.place_flag_4fcf88)
#undef ADDR_PLACE_FACING
#define ADDR_PLACE_FACING ((uintptr_t)(const void *)&am2_origstate.place_facing)
#undef ADDR_NUMBER_KEY_SLOT
#define ADDR_NUMBER_KEY_SLOT ((uintptr_t)(const void *)&am2_origstate.number_key_slot)
#undef ADDR_CHEAT_ENABLED
#define ADDR_CHEAT_ENABLED ((uintptr_t)(const void *)&am2_origstate.cheat_enabled)
#undef ADDR_CHEAT_LEVEL_SELECT
#define ADDR_CHEAT_LEVEL_SELECT ((uintptr_t)(const void *)&am2_origstate.cheat_level_select)
#undef ADDR_OUR_POINTS
#define ADDR_OUR_POINTS ((uintptr_t)(const void *)&am2_origstate.our_points)
#undef ADDR_FLAME_ON
#define ADDR_FLAME_ON ((uintptr_t)(const void *)&am2_origstate.flame_on)
#undef ADDR_FLAME_NEXT_MS
#define ADDR_FLAME_NEXT_MS ((uintptr_t)(const void *)&am2_origstate.flame_next_ms)
#undef ADDR_VIEW_COLOUR_COPY
#define ADDR_VIEW_COLOUR_COPY ((uintptr_t)(const void *)&am2_origstate.view_colour_copy)
#undef ADDR_DEBUG_ITEMLIST
#define ADDR_DEBUG_ITEMLIST ((uintptr_t)(const void *)&am2_origstate.opt_rob)
#undef ADDR_OPT_ROB
#define ADDR_OPT_ROB ((uintptr_t)(const void *)&am2_origstate.opt_rob)
#undef ADDR_OPT_DAN
#define ADDR_OPT_DAN ((uintptr_t)(const void *)&am2_origstate.opt_dan)
#undef ADDR_OPT_PETER
#define ADDR_OPT_PETER ((uintptr_t)(const void *)&am2_origstate.opt_peter)
#undef ADDR_OPT_4FD748
#define ADDR_OPT_4FD748 ((uintptr_t)(const void *)&am2_origstate.opt_4fd748)
#undef ADDR_COLOUR_DARK_BLUE
#define ADDR_COLOUR_DARK_BLUE ((uintptr_t)(const void *)&am2_origstate.colour_dark_blue)
#undef ADDR_REMAP_IDENTITY
#define ADDR_REMAP_IDENTITY ((uintptr_t)(const void *)&am2_origstate.remap_identity)
#undef ADDR_COLOUR_WHITE
#define ADDR_COLOUR_WHITE ((uintptr_t)(const void *)&am2_origstate.colour_white)
#undef ADDR_COLOUR_DARK_GREEN
#define ADDR_COLOUR_DARK_GREEN ((uintptr_t)(const void *)&am2_origstate.colour_dark_green)
#undef ADDR_COLOUR_CREAM
#define ADDR_COLOUR_CREAM ((uintptr_t)(const void *)&am2_origstate.colour_cream)
#undef ADDR_DIRECTDRAW
#define ADDR_DIRECTDRAW ((uintptr_t)(const void *)&am2_origstate.directdraw)
#undef ADDR_COLOUR_BLUE
#define ADDR_COLOUR_BLUE ((uintptr_t)(const void *)&am2_origstate.colour_blue)
#undef ADDR_SURFACE_LOCKED
#define ADDR_SURFACE_LOCKED ((uintptr_t)(const void *)&am2_origstate.surface_locked)
#undef ADDR_DEFAULT_PALETTE
#define ADDR_DEFAULT_PALETTE ((uintptr_t)(const void *)&am2_origstate.default_palette)
#undef ADDR_REMAP_DARK
#define ADDR_REMAP_DARK ((uintptr_t)(const void *)&am2_origstate.default_palette)
#undef ADDR_COLOUR_STEEL_BLUE
#define ADDR_COLOUR_STEEL_BLUE ((uintptr_t)(const void *)&am2_origstate.colour_steel_blue)
#undef ADDR_VIEW_RECT_COLOUR
#define ADDR_VIEW_RECT_COLOUR ((uintptr_t)(const void *)&am2_origstate.view_rect_colour)
#undef ADDR_BACK_BUFFER
#define ADDR_BACK_BUFFER ((uintptr_t)(const void *)&am2_origstate.back_buffer)
#undef ADDR_COLOUR_STALE
#define ADDR_COLOUR_STALE ((uintptr_t)(const void *)&am2_origstate.colour_stale)
#undef ADDR_COLOUR_LIGHT_GREEN
#define ADDR_COLOUR_LIGHT_GREEN ((uintptr_t)(const void *)&am2_origstate.colour_light_green)
#undef ADDR_COLOUR_LAG_MID
#define ADDR_COLOUR_LAG_MID ((uintptr_t)(const void *)&am2_origstate.colour_lag_mid)
#undef ADDR_COLOUR_LIGHT_GREY
#define ADDR_COLOUR_LIGHT_GREY ((uintptr_t)(const void *)&am2_origstate.colour_light_grey)
#undef ADDR_COLOUR_BLACK
#define ADDR_COLOUR_BLACK ((uintptr_t)(const void *)&am2_origstate.colour_black)
#undef ADDR_DIRECTDRAW2
#define ADDR_DIRECTDRAW2 ((uintptr_t)(const void *)&am2_origstate.directdraw2)
#undef ADDR_REMAP_BRIGHT_BUF
#define ADDR_REMAP_BRIGHT_BUF ((uintptr_t)(const void *)&am2_origstate.remap_bright_buf)
#undef ADDR_OVERLAY_PALETTE
#define ADDR_OVERLAY_PALETTE ((uintptr_t)(const void *)&am2_origstate.overlay_palette)
#undef ADDR_FRAMEBUFFER
#define ADDR_FRAMEBUFFER ((uintptr_t)(const void *)&am2_origstate.framebuffer)
#undef ADDR_COLOUR_LIGHT_BLUE
#define ADDR_COLOUR_LIGHT_BLUE ((uintptr_t)(const void *)&am2_origstate.colour_light_blue)
#undef ADDR_COLOUR_WHITE_B
#define ADDR_COLOUR_WHITE_B ((uintptr_t)(const void *)&am2_origstate.colour_white_b)
#undef ADDR_COLOUR_OLIVE
#define ADDR_COLOUR_OLIVE ((uintptr_t)(const void *)&am2_origstate.colour_olive)
#undef ADDR_REMAP_SHADES
#define ADDR_REMAP_SHADES ((uintptr_t)(const void *)&am2_origstate.remap_shades)
#undef ADDR_VARIATION_BLOCKS
#define ADDR_VARIATION_BLOCKS ((uintptr_t)(const void *)&am2_origstate.variation_blocks)
#undef ADDR_VARIATION_END
#define ADDR_VARIATION_END ((uintptr_t)(const void *)&am2_origstate.variation_end)
#undef ADDR_PALETTE_COPY
#define ADDR_PALETTE_COPY ((uintptr_t)(const void *)&am2_origstate.palette_copy)
#undef ADDR_LIST_INK_HOT_SEL
#define ADDR_LIST_INK_HOT_SEL ((uintptr_t)(const void *)&am2_origstate.list_ink_hot_sel)
#undef ADDR_SCREEN_PITCH
#define ADDR_SCREEN_PITCH ((uintptr_t)(const void *)&am2_origstate.screen_pitch)
#undef ADDR_PRIMARY_SURFACE
#define ADDR_PRIMARY_SURFACE ((uintptr_t)(const void *)&am2_origstate.primary_surface)
#undef ADDR_COLOUR_BELOW_BG
#define ADDR_COLOUR_BELOW_BG ((uintptr_t)(const void *)&am2_origstate.colour_below_bg)
#undef ADDR_BACKGROUND_COLOUR
#define ADDR_BACKGROUND_COLOUR ((uintptr_t)(const void *)&am2_origstate.background_colour)
#undef ADDR_DEFAULT_PALETTE_BUF
#define ADDR_DEFAULT_PALETTE_BUF ((uintptr_t)(const void *)&am2_origstate.default_palette_buf)
#undef ADDR_COLOUR_DARK_GREY
#define ADDR_COLOUR_DARK_GREY ((uintptr_t)(const void *)&am2_origstate.colour_dark_grey)
#undef ADDR_COLOUR_NO_MAP
#define ADDR_COLOUR_NO_MAP ((uintptr_t)(const void *)&am2_origstate.colour_no_map)
#undef ADDR_REMAP_SHADE_STORE
#define ADDR_REMAP_SHADE_STORE ((uintptr_t)(const void *)&am2_origstate.remap_shade_store)
#undef ADDR_OFFSCREEN_SURFACE
#define ADDR_OFFSCREEN_SURFACE ((uintptr_t)(const void *)&am2_origstate.offscreen_surface)
#undef ADDR_TILESET_PALETTES
#define ADDR_TILESET_PALETTES ((uintptr_t)(const void *)&am2_origstate.tileset_palettes)
#undef ADDR_DRAW_TARGET
#define ADDR_DRAW_TARGET ((uintptr_t)(const void *)&am2_origstate.draw_target)
#undef ADDR_KIND7_NAMES
#define ADDR_KIND7_NAMES ((uintptr_t)(const void *)&am2_origstate.kind7_names)
#undef ADDR_VARIATION_TABLE
#define ADDR_VARIATION_TABLE ((uintptr_t)(const void *)&am2_origstate.variation_table)
#undef ADDR_REMAP_BRIGHT
#define ADDR_REMAP_BRIGHT ((uintptr_t)(const void *)&am2_origstate.remap_bright)
#undef ADDR_HUD_MESSAGE_COLOUR
#define ADDR_HUD_MESSAGE_COLOUR ((uintptr_t)(const void *)&am2_origstate.hud_message_colour)
#undef ADDR_REMAP_IDENTITY_BUF
#define ADDR_REMAP_IDENTITY_BUF ((uintptr_t)(const void *)&am2_origstate.remap_identity_buf)
#undef ADDR_DD_CLIPPER
#define ADDR_DD_CLIPPER ((uintptr_t)(const void *)&am2_origstate.dd_clipper)
#undef ADDR_OPT_WINDOWED
#define ADDR_OPT_WINDOWED ((uintptr_t)(const void *)&am2_origstate.opt_windowed)
#undef ADDR_DEPTH_CURSOR
#define ADDR_DEPTH_CURSOR ((uintptr_t)(const void *)&am2_origstate.depth_cursor)
#undef ADDR_DEPTH_NODES
#define ADDR_DEPTH_NODES ((uintptr_t)(const void *)&am2_origstate.depth_nodes)
#undef ADDR_DEPTH_COUNT
#define ADDR_DEPTH_COUNT ((uintptr_t)(const void *)&am2_origstate.depth_count)
#undef ADDR_DEPTH_HEAD
#define ADDR_DEPTH_HEAD ((uintptr_t)(const void *)&am2_origstate.depth_head)
#undef ADDR_DEPTH_FIELD_DC
#define ADDR_DEPTH_FIELD_DC ((uintptr_t)(const void *)&am2_origstate.depth_field_dc)
#undef ADDR_ERROR_TEXT_DD
#define ADDR_ERROR_TEXT_DD ((uintptr_t)(const void *)&am2_origstate.error_text_dd)
#undef ADDR_ERROR_TEXT
#define ADDR_ERROR_TEXT ((uintptr_t)(const void *)&am2_origstate.error_text)
#undef ADDR_LEAK_TOTAL
#define ADDR_LEAK_TOTAL ((uintptr_t)(const void *)&am2_origstate.leak_total)
#undef ADDR_LEAK_COUNT
#define ADDR_LEAK_COUNT ((uintptr_t)(const void *)&am2_origstate.leak_count)
#undef ADDR_LEAK_RECORDS
#define ADDR_LEAK_RECORDS ((uintptr_t)(const void *)&am2_origstate.leak_records)
#undef ADDR_UNREAD_50C34C
#define ADDR_UNREAD_50C34C ((uintptr_t)(const void *)&am2_origstate.unread_50c34c)
#undef ADDR_SCRIPT_RELOADING
#define ADDR_SCRIPT_RELOADING ((uintptr_t)(const void *)&am2_origstate.script_reloading)
#undef ADDR_OPT_TRACE_WIN
#define ADDR_OPT_TRACE_WIN ((uintptr_t)(const void *)&am2_origstate.opt_trace_win)
#undef ADDR_OPT_DBG
#define ADDR_OPT_DBG ((uintptr_t)(const void *)&am2_origstate.opt_dbg)
#undef ADDR_OPT_TRACE_VEH
#define ADDR_OPT_TRACE_VEH ((uintptr_t)(const void *)&am2_origstate.opt_trace_veh)
#undef ADDR_TIMER_TABLE_ID_END
#define ADDR_TIMER_TABLE_ID_END ((uintptr_t)(const void *)&am2_origstate.timer_table_id_end)
#undef ADDR_RULE_UID_A
#define ADDR_RULE_UID_A ((uintptr_t)(const void *)&am2_origstate.rule_uid_a)
#undef ADDR_RULE_UID_B
#define ADDR_RULE_UID_B ((uintptr_t)(const void *)&am2_origstate.rule_uid_b)
#undef ADDR_RULE_UID_C
#define ADDR_RULE_UID_C ((uintptr_t)(const void *)&am2_origstate.rule_uid_c)
#undef ADDR_EXPLOSION_ANIMS
#define ADDR_EXPLOSION_ANIMS ((uintptr_t)(const void *)&am2_origstate.explosion_anims)
#undef ADDR_STATE0_TICK
#define ADDR_STATE0_TICK ((uintptr_t)(const void *)&am2_origstate.state0_tick)
#undef ADDR_MOVIE_TO_PLAY
#define ADDR_MOVIE_TO_PLAY ((uintptr_t)(const void *)&am2_origstate.movie_to_play)
#undef ADDR_SCRIPT_RELOAD_PATH
#define ADDR_SCRIPT_RELOAD_PATH ((uintptr_t)(const void *)&am2_origstate.script_reload_path)
#undef ADDR_MAP_CHECKSUM_VAL
#define ADDR_MAP_CHECKSUM_VAL ((uintptr_t)(const void *)&am2_origstate.map_checksum_val)
#undef ADDR_MP_SCRIPT_CHKSUM_VAL
#define ADDR_MP_SCRIPT_CHKSUM_VAL ((uintptr_t)(const void *)&am2_origstate.mp_script_chksum_val)
#undef ADDR_RULES_CHECKSUM_VAL
#define ADDR_RULES_CHECKSUM_VAL ((uintptr_t)(const void *)&am2_origstate.rules_checksum_val)
#undef ADDR_LEVEL_ID
#define ADDR_LEVEL_ID ((uintptr_t)(const void *)&am2_origstate.level_id)
#undef ADDR_GAME_STATE
#define ADDR_GAME_STATE ((uintptr_t)(const void *)&am2_origstate.game_state)
#undef ADDR_STATE_ENTERED
#define ADDR_STATE_ENTERED ((uintptr_t)(const void *)&am2_origstate.state_entered)
#undef ADDR_STATE_PENDING
#define ADDR_STATE_PENDING ((uintptr_t)(const void *)&am2_origstate.state_pending)
#undef ADDR_STATE_WANTED
#define ADDR_STATE_WANTED ((uintptr_t)(const void *)&am2_origstate.state_wanted)
#undef ADDR_GAME_STATE_ARG
#define ADDR_GAME_STATE_ARG ((uintptr_t)(const void *)&am2_origstate.game_state_arg)
#undef ADDR_MENU_REQUEST_SET
#define ADDR_MENU_REQUEST_SET ((uintptr_t)(const void *)&am2_origstate.menu_request_set)
#undef ADDR_MENU_REQUEST
#define ADDR_MENU_REQUEST ((uintptr_t)(const void *)&am2_origstate.menu_request)
#undef ADDR_SCRIPT_RELOAD
#define ADDR_SCRIPT_RELOAD ((uintptr_t)(const void *)&am2_origstate.script_reload)
#undef ADDR_STATE_ENTER_ONCE
#define ADDR_STATE_ENTER_ONCE ((uintptr_t)(const void *)&am2_origstate.state_enter_once)
#undef ADDR_NET_GAME
#define ADDR_NET_GAME ((uintptr_t)(const void *)&am2_origstate.net_game)
#undef ADDR_LOAD_PENDING
#define ADDR_LOAD_PENDING ((uintptr_t)(const void *)&am2_origstate.load_pending)
#undef ADDR_HAVE_DEFAULT_COF
#define ADDR_HAVE_DEFAULT_COF ((uintptr_t)(const void *)&am2_origstate.have_default_cof)
#undef ADDR_UID_COUNTERS
#define ADDR_UID_COUNTERS ((uintptr_t)(const void *)&am2_origstate.uid_counters)
#undef ADDR_CLOCK_BASE_MS
#define ADDR_CLOCK_BASE_MS ((uintptr_t)(const void *)&am2_origstate.clock_base_ms)
#undef ADDR_FRAME_DELTA_MS
#define ADDR_FRAME_DELTA_MS ((uintptr_t)(const void *)&am2_origstate.frame_delta_ms)
#undef ADDR_LAST_TICK_MS
#define ADDR_LAST_TICK_MS ((uintptr_t)(const void *)&am2_origstate.last_tick_ms)
#undef ADDR_FRAME_DELTA_SEC
#define ADDR_FRAME_DELTA_SEC ((uintptr_t)(const void *)&am2_origstate.frame_delta_sec)
#undef ADDR_GAME_OVER_SOURCE
#define ADDR_GAME_OVER_SOURCE ((uintptr_t)(const void *)&am2_origstate.game_over_source)
#undef ADDR_OBJ_CTX_VAL_A
#define ADDR_OBJ_CTX_VAL_A ((uintptr_t)(const void *)&am2_origstate.obj_ctx_val_a)
#undef ADDR_LEVEL_FLAG_E30
#define ADDR_LEVEL_FLAG_E30 ((uintptr_t)(const void *)&am2_origstate.level_flag_e30)
#undef ADDR_VIEW_SNAP
#define ADDR_VIEW_SNAP ((uintptr_t)(const void *)&am2_origstate.view_snap)
#undef ADDR_VIEW_HOLD
#define ADDR_VIEW_HOLD ((uintptr_t)(const void *)&am2_origstate.view_hold)
#undef ADDR_INPUT_SUPPRESS
#define ADDR_INPUT_SUPPRESS ((uintptr_t)(const void *)&am2_origstate.input_suppress)
#undef ADDR_OUR_LEADER_UID
#define ADDR_OUR_LEADER_UID ((uintptr_t)(const void *)&am2_origstate.our_leader_uid)
#undef ADDR_LEADER_POS
#define ADDR_LEADER_POS ((uintptr_t)(const void *)&am2_origstate.leader_pos)
#undef ADDR_LEADER_FACING
#define ADDR_LEADER_FACING ((uintptr_t)(const void *)&am2_origstate.leader_facing)
#undef ADDR_WEAPON_OWNER_ID
#define ADDR_WEAPON_OWNER_ID ((uintptr_t)(const void *)&am2_origstate.weapon_owner_id)
#undef ADDR_WEAPON_SLOT
#define ADDR_WEAPON_SLOT ((uintptr_t)(const void *)&am2_origstate.weapon_slot)
#undef ADDR_MESSAGE_TEXT
#define ADDR_MESSAGE_TEXT ((uintptr_t)(const void *)&am2_origstate.message_text)
#undef ADDR_OBJ_CTX_OBJ_A
#define ADDR_OBJ_CTX_OBJ_A ((uintptr_t)(const void *)&am2_origstate.obj_ctx_obj_a)
#undef ADDR_WEAPON_FN_SLOT0
#define ADDR_WEAPON_FN_SLOT0 ((uintptr_t)(const void *)&am2_origstate.weapon_fn_slot0)
#undef ADDR_WEAPON_FN_SLOT1
#define ADDR_WEAPON_FN_SLOT1 ((uintptr_t)(const void *)&am2_origstate.weapon_fn_slot1)
#undef ADDR_WEAPON_FN_SLOT3
#define ADDR_WEAPON_FN_SLOT3 ((uintptr_t)(const void *)&am2_origstate.weapon_fn_slot3)
#undef ADDR_POINTER_PICK
#define ADDR_POINTER_PICK ((uintptr_t)(const void *)&am2_origstate.pointer_pick)
#undef ADDR_POINTER_ACTION
#define ADDR_POINTER_ACTION ((uintptr_t)(const void *)&am2_origstate.pointer_action)
#undef ADDR_POINTER_F10
#define ADDR_POINTER_F10 ((uintptr_t)(const void *)&am2_origstate.pointer_f10)
#undef ADDR_POINTER_F14
#define ADDR_POINTER_F14 ((uintptr_t)(const void *)&am2_origstate.pointer_f14)
#undef ADDR_WEAPON_FN_SLOT2
#define ADDR_WEAPON_FN_SLOT2 ((uintptr_t)(const void *)&am2_origstate.weapon_fn_slot2)
#undef ADDR_POINTER_OVERLAY
#define ADDR_POINTER_OVERLAY ((uintptr_t)(const void *)&am2_origstate.pointer_overlay)
#undef ADDR_SECOND_DEADLINE
#define ADDR_SECOND_DEADLINE ((uintptr_t)(const void *)&am2_origstate.second_deadline)
#undef ADDR_PAUSE_FLAGS
#define ADDR_PAUSE_FLAGS ((uintptr_t)(const void *)&am2_origstate.pause_flags)
#undef ADDR_GAME_WINNER
#define ADDR_GAME_WINNER ((uintptr_t)(const void *)&am2_origstate.game_winner)
#undef ADDR_WIN_ENABLED
#define ADDR_WIN_ENABLED ((uintptr_t)(const void *)&am2_origstate.win_enabled)
#undef ADDR_SELECTED_UIDS
#define ADDR_SELECTED_UIDS ((uintptr_t)(const void *)&am2_origstate.selected_uids)
#undef ADDR_SELECTED_COUNT
#define ADDR_SELECTED_COUNT ((uintptr_t)(const void *)&am2_origstate.selected_count)
#undef ADDR_SELECTED_ITEMS
#define ADDR_SELECTED_ITEMS ((uintptr_t)(const void *)&am2_origstate.selected_items)
#undef ADDR_GAME_SEED
#define ADDR_GAME_SEED ((uintptr_t)(const void *)&am2_origstate.game_seed)
#undef ADDR_MISSION_RETRY
#define ADDR_MISSION_RETRY ((uintptr_t)(const void *)&am2_origstate.mission_retry)
#undef ADDR_ATTEMPT_COUNT
#define ADDR_ATTEMPT_COUNT ((uintptr_t)(const void *)&am2_origstate.attempt_count)
#undef ADDR_LEVEL_ATTEMPT
#define ADDR_LEVEL_ATTEMPT ((uintptr_t)(const void *)&am2_origstate.attempt_count)
#undef ADDR_STARTUP_COLOURS
#define ADDR_STARTUP_COLOURS ((uintptr_t)(const void *)&am2_origstate.startup_colours)
#undef ADDR_NET_SETTLE_COUNT
#define ADDR_NET_SETTLE_COUNT ((uintptr_t)(const void *)&am2_origstate.net_settle_count)
#undef ADDR_PERF_FREQ
#define ADDR_PERF_FREQ ((uintptr_t)(const void *)&am2_origstate.perf_freq)
#undef ADDR_CHEAT_INVULNERABLE
#define ADDR_CHEAT_INVULNERABLE ((uintptr_t)(const void *)&am2_origstate.cheat_invulnerable)
#undef ADDR_AIM_X
#define ADDR_AIM_X ((uintptr_t)(const void *)&am2_origstate.aim_x)
#undef ADDR_AIM_Y
#define ADDR_AIM_Y ((uintptr_t)(const void *)&am2_origstate.aim_y)
#undef ADDR_AIM_Z
#define ADDR_AIM_Z ((uintptr_t)(const void *)&am2_origstate.aim_z)
#undef ADDR_PERF_PERIOD
#define ADDR_PERF_PERIOD ((uintptr_t)(const void *)&am2_origstate.perf_period)
#undef ADDR_PERF_START
#define ADDR_PERF_START ((uintptr_t)(const void *)&am2_origstate.perf_start)
#undef ADDR_HINSTANCE
#define ADDR_HINSTANCE ((uintptr_t)(const void *)&am2_origstate.hinstance)
#undef ADDR_CD_FOUND_FLAG
#define ADDR_CD_FOUND_FLAG ((uintptr_t)(const void *)&am2_origstate.cd_found_flag)
#undef ADDR_FIXED_STEP
#define ADDR_FIXED_STEP ((uintptr_t)(const void *)&am2_origstate.fixed_step)
#undef ADDR_OPT_NO_MOVIES
#define ADDR_OPT_NO_MOVIES ((uintptr_t)(const void *)&am2_origstate.opt_no_movies)
#undef ADDR_ZERO_RECT
#define ADDR_ZERO_RECT ((uintptr_t)(const void *)&am2_origstate.zero_rect)
#undef ADDR_CHAR_HANDLER
#define ADDR_CHAR_HANDLER ((uintptr_t)(const void *)&am2_origstate.char_handler)
#undef ADDR_KEYS_BUFFER_A
#define ADDR_KEYS_BUFFER_A ((uintptr_t)(const void *)&am2_origstate.keys_buffer_a)
#undef ADDR_KEYS_BUFFER_B
#define ADDR_KEYS_BUFFER_B ((uintptr_t)(const void *)&am2_origstate.keys_buffer_b)
#undef ADDR_KEYS_NOW_PTR
#define ADDR_KEYS_NOW_PTR ((uintptr_t)(const void *)&am2_origstate.keys_now_ptr)
#undef ADDR_KEYS_PREV_PTR
#define ADDR_KEYS_PREV_PTR ((uintptr_t)(const void *)&am2_origstate.keys_prev_ptr)
#undef ADDR_KEY_REPEAT_AT
#define ADDR_KEY_REPEAT_AT ((uintptr_t)(const void *)&am2_origstate.key_repeat_at)
#undef ADDR_KEY_PRESSED
#define ADDR_KEY_PRESSED ((uintptr_t)(const void *)&am2_origstate.key_pressed)
#undef ADDR_DINPUT
#define ADDR_DINPUT ((uintptr_t)(const void *)&am2_origstate.dinput)
#undef ADDR_DI_MOUSE
#define ADDR_DI_MOUSE ((uintptr_t)(const void *)&am2_origstate.di_mouse)
#undef ADDR_DI_KEYBOARD
#define ADDR_DI_KEYBOARD ((uintptr_t)(const void *)&am2_origstate.di_keyboard)
#undef ADDR_DI_DEVICE_3
#define ADDR_DI_DEVICE_3 ((uintptr_t)(const void *)&am2_origstate.di_device_3)
#undef ADDR_DI_MOUSE_ACQUIRED
#define ADDR_DI_MOUSE_ACQUIRED ((uintptr_t)(const void *)&am2_origstate.di_mouse_acquired)
#undef ADDR_ITEM_HEADER_SIZE
#define ADDR_ITEM_HEADER_SIZE ((uintptr_t)(const void *)&am2_origstate.item_header_size)
#undef ADDR_UID_REMAP_CAP
#define ADDR_UID_REMAP_CAP ((uintptr_t)(const void *)&am2_origstate.uid_remap_cap)
#undef ADDR_UID_REMAP_COUNT
#define ADDR_UID_REMAP_COUNT ((uintptr_t)(const void *)&am2_origstate.uid_remap_count)
#undef ADDR_UID_REMAP
#define ADDR_UID_REMAP ((uintptr_t)(const void *)&am2_origstate.uid_remap)
#undef ADDR_ITER_STAMP
#define ADDR_ITER_STAMP ((uintptr_t)(const void *)&am2_origstate.iter_stamp)
#undef ADDR_MAP_EXTENT_X
#define ADDR_MAP_EXTENT_X ((uintptr_t)(const void *)&am2_origstate.map_extent_x)
#undef ADDR_MAP_EXTENT_Y
#define ADDR_MAP_EXTENT_Y ((uintptr_t)(const void *)&am2_origstate.map_extent_y)
#undef ADDR_MAP_EXTENT_SHIFT
#define ADDR_MAP_EXTENT_SHIFT ((uintptr_t)(const void *)&am2_origstate.map_extent_shift)
#undef ADDR_MAP_ROW_SHIFT
#define ADDR_MAP_ROW_SHIFT ((uintptr_t)(const void *)&am2_origstate.map_row_shift)
#undef ADDR_MAP_BOUNDS
#define ADDR_MAP_BOUNDS ((uintptr_t)(const void *)&am2_origstate.map_bounds)
#undef ADDR_VIEW_TARGET
#define ADDR_VIEW_TARGET ((uintptr_t)(const void *)&am2_origstate.view_target)
#undef ADDR_LISTENER_POS
#define ADDR_LISTENER_POS ((uintptr_t)(const void *)&am2_origstate.listener_pos)
#undef ADDR_LISTENER_POS_PREV
#define ADDR_LISTENER_POS_PREV ((uintptr_t)(const void *)&am2_origstate.listener_pos_prev)
#undef ADDR_VIEW_ORIGIN_X
#define ADDR_VIEW_ORIGIN_X ((uintptr_t)(const void *)&am2_origstate.view_origin_x)
#undef ADDR_VIEW_ORIGIN_Y
#define ADDR_VIEW_ORIGIN_Y ((uintptr_t)(const void *)&am2_origstate.view_origin_y)
#undef ADDR_VIEW_FAR_X
#define ADDR_VIEW_FAR_X ((uintptr_t)(const void *)&am2_origstate.view_far_x)
#undef ADDR_VIEW_FAR_Y
#define ADDR_VIEW_FAR_Y ((uintptr_t)(const void *)&am2_origstate.view_far_y)
#undef ADDR_SECOND_RECT
#define ADDR_SECOND_RECT ((uintptr_t)(const void *)&am2_origstate.second_rect)
#undef ADDR_SECOND_RECT_PREV
#define ADDR_SECOND_RECT_PREV ((uintptr_t)(const void *)&am2_origstate.second_rect_prev)
#undef ADDR_VIEW_CLIPPED
#define ADDR_VIEW_CLIPPED ((uintptr_t)(const void *)&am2_origstate.view_clipped)
#undef ADDR_SHAKE_TIME
#define ADDR_SHAKE_TIME ((uintptr_t)(const void *)&am2_origstate.shake_time)
#undef ADDR_SHAKE_PHASE_X
#define ADDR_SHAKE_PHASE_X ((uintptr_t)(const void *)&am2_origstate.shake_phase_x)
#undef ADDR_SHAKE_STEP_X
#define ADDR_SHAKE_STEP_X ((uintptr_t)(const void *)&am2_origstate.shake_step_x)
#undef ADDR_SHAKE_PHASE_Y
#define ADDR_SHAKE_PHASE_Y ((uintptr_t)(const void *)&am2_origstate.shake_phase_y)
#undef ADDR_SHAKE_STEP_Y
#define ADDR_SHAKE_STEP_Y ((uintptr_t)(const void *)&am2_origstate.shake_step_y)
#undef ADDR_SHAKE_AMPLITUDE
#define ADDR_SHAKE_AMPLITUDE ((uintptr_t)(const void *)&am2_origstate.shake_amplitude)
#undef ADDR_MAP_SURFACE
#define ADDR_MAP_SURFACE ((uintptr_t)(const void *)&am2_origstate.map_surface)
#undef ADDR_MAP_CACHE_SURFACE
#define ADDR_MAP_CACHE_SURFACE ((uintptr_t)(const void *)&am2_origstate.map_cache_surface)
#undef ADDR_TILE_SHIFT_LOG
#define ADDR_TILE_SHIFT_LOG ((uintptr_t)(const void *)&am2_origstate.tile_shift_log)
#undef ADDR_VIEW_TILES_W
#define ADDR_VIEW_TILES_W ((uintptr_t)(const void *)&am2_origstate.view_tiles_w)
#undef ADDR_VIEW_TILES_H
#define ADDR_VIEW_TILES_H ((uintptr_t)(const void *)&am2_origstate.view_tiles_h)
#undef ADDR_CAMERA_X
#define ADDR_CAMERA_X ((uintptr_t)(const void *)&am2_origstate.visible_tiles)
#undef ADDR_VISIBLE_TILES
#define ADDR_VISIBLE_TILES ((uintptr_t)(const void *)&am2_origstate.visible_tiles)
#undef ADDR_CAMERA_Y
#define ADDR_CAMERA_Y ((uintptr_t)(const void *)&am2_origstate.camera_y)
#undef ADDR_MAP_TILES
#define ADDR_MAP_TILES ((uintptr_t)(const void *)&am2_origstate.map_tiles)
#undef ADDR_TILE_ATTRS
#define ADDR_TILE_ATTRS ((uintptr_t)(const void *)&am2_origstate.tile_attrs)
#undef ADDR_CELL_WEIGHTS
#define ADDR_CELL_WEIGHTS ((uintptr_t)(const void *)&am2_origstate.cell_weights)
#undef ADDR_TILE_FLAGS
#define ADDR_TILE_FLAGS ((uintptr_t)(const void *)&am2_origstate.tile_flags)
#undef ADDR_TILE_KIND
#define ADDR_TILE_KIND ((uintptr_t)(const void *)&am2_origstate.tile_kind)
#undef ADDR_TILE_REVEAL_GRIDS
#define ADDR_TILE_REVEAL_GRIDS ((uintptr_t)(const void *)&am2_origstate.tile_reveal_grids)
#undef ADDR_TILE_COVER
#define ADDR_TILE_COVER ((uintptr_t)(const void *)&am2_origstate.tile_cover)
#undef ADDR_OBJ_CAPACITY
#define ADDR_OBJ_CAPACITY ((uintptr_t)(const void *)&am2_origstate.obj_capacity)
#undef ADDR_OBJ_COUNT
#define ADDR_OBJ_COUNT ((uintptr_t)(const void *)&am2_origstate.obj_count)
#undef ADDR_ITER_CURSOR
#define ADDR_ITER_CURSOR ((uintptr_t)(const void *)&am2_origstate.iter_cursor)
#undef ADDR_OBJ_TABLE
#define ADDR_OBJ_TABLE ((uintptr_t)(const void *)&am2_origstate.obj_table)
#undef ADDR_PALETTE_CYCLE_STAMP
#define ADDR_PALETTE_CYCLE_STAMP ((uintptr_t)(const void *)&am2_origstate.palette_cycle_stamp)
#undef ADDR_GAME_OVER_STATE
#define ADDR_GAME_OVER_STATE ((uintptr_t)(const void *)&am2_origstate.game_over_state)
#undef ADDR_GAME_OVER_SAVED
#define ADDR_GAME_OVER_SAVED ((uintptr_t)(const void *)&am2_origstate.game_over_saved)
#undef ADDR_STATE_MOVIE
#define ADDR_STATE_MOVIE ((uintptr_t)(const void *)&am2_origstate.state_movie)
#undef ADDR_MP_PANEL_SPRITES_B
#define ADDR_MP_PANEL_SPRITES_B ((uintptr_t)(const void *)&am2_origstate.mp_panel_sprites_b)
#undef ADDR_MP_PANEL_SPRITES_B_END
#define ADDR_MP_PANEL_SPRITES_B_END ((uintptr_t)(const void *)&am2_origstate.mp_panel_sprites_b_end)
#undef ADDR_GAME_OVER_FLAGS
#define ADDR_GAME_OVER_FLAGS ((uintptr_t)(const void *)&am2_origstate.game_over_flags)
#undef ADDR_GAME_SETTING_22C
#define ADDR_GAME_SETTING_22C ((uintptr_t)(const void *)&am2_origstate.game_setting_22c)
#undef ADDR_ARMY_POINTS
#define ADDR_ARMY_POINTS ((uintptr_t)(const void *)&am2_origstate.army_points)
#undef ADDR_LIVE_PLAYER_NAME
#define ADDR_LIVE_PLAYER_NAME ((uintptr_t)(const void *)&am2_origstate.live_player_name)
#undef ADDR_LIVE_BATTLE_NAME
#define ADDR_LIVE_BATTLE_NAME ((uintptr_t)(const void *)&am2_origstate.live_battle_name)
#undef ADDR_MP_PANEL_SPRITES_A
#define ADDR_MP_PANEL_SPRITES_A ((uintptr_t)(const void *)&am2_origstate.mp_panel_sprites_a)
#undef ADDR_MENU_MSG_LIST
#define ADDR_MENU_MSG_LIST ((uintptr_t)(const void *)&am2_origstate.menu_msg_list)
#undef ADDR_SESSION_OBJECT
#define ADDR_SESSION_OBJECT ((uintptr_t)(const void *)&am2_origstate.session_object)
#undef ADDR_RECORD_LIST_CAP
#define ADDR_RECORD_LIST_CAP ((uintptr_t)(const void *)&am2_origstate.record_list_cap)
#undef ADDR_RECORD_LIST_COUNT
#define ADDR_RECORD_LIST_COUNT ((uintptr_t)(const void *)&am2_origstate.record_list_count)
#undef ADDR_RECORD_LISTS
#define ADDR_RECORD_LISTS ((uintptr_t)(const void *)&am2_origstate.record_lists)
#undef ADDR_AAI_RECORD_CAP
#define ADDR_AAI_RECORD_CAP ((uintptr_t)(const void *)&am2_origstate.aai_record_cap)
#undef ADDR_KEY_TABLE_COUNT
#define ADDR_KEY_TABLE_COUNT ((uintptr_t)(const void *)&am2_origstate.key_table_count)
#undef ADDR_AAI_RECORDS
#define ADDR_AAI_RECORDS ((uintptr_t)(const void *)&am2_origstate.aai_records)
#undef ADDR_KEY_TABLE
#define ADDR_KEY_TABLE ((uintptr_t)(const void *)&am2_origstate.key_table)
#undef ADDR_RECORD_LIST_INDEX
#define ADDR_RECORD_LIST_INDEX ((uintptr_t)(const void *)&am2_origstate.record_list_index)
#undef ADDR_AAI_KEY_980000
#define ADDR_AAI_KEY_980000 ((uintptr_t)(const void *)&am2_origstate.aai_key_980000)
#undef ADDR_AAI_KEY_980080
#define ADDR_AAI_KEY_980080 ((uintptr_t)(const void *)&am2_origstate.aai_key_980080)
#undef ADDR_CREATE_WATCHED_KIND
#define ADDR_CREATE_WATCHED_KIND ((uintptr_t)(const void *)&am2_origstate.create_watched_kind)
#undef ADDR_WATCHED_TYPE_ID
#define ADDR_WATCHED_TYPE_ID ((uintptr_t)(const void *)&am2_origstate.watched_type_id)
#undef ADDR_AAI_KEY_980100
#define ADDR_AAI_KEY_980100 ((uintptr_t)(const void *)&am2_origstate.aai_key_980100)
#undef ADDR_KIND7_COUNT
#define ADDR_KIND7_COUNT ((uintptr_t)(const void *)&am2_origstate.kind7_count)
#undef ADDR_REGION_INSERT_PREV
#define ADDR_REGION_INSERT_PREV ((uintptr_t)(const void *)&am2_origstate.region_insert_prev)
#undef ADDR_REGION_NEIGHBOUR
#define ADDR_REGION_NEIGHBOUR ((uintptr_t)(const void *)&am2_origstate.region_neighbour)
#undef ADDR_TILE_STEP8
#define ADDR_TILE_STEP8 ((uintptr_t)(const void *)&am2_origstate.tile_step8)
#undef ADDR_REGION_GOAL
#define ADDR_REGION_GOAL ((uintptr_t)(const void *)&am2_origstate.region_goal)
#undef ADDR_REGION_OPEN_HEAD
#define ADDR_REGION_OPEN_HEAD ((uintptr_t)(const void *)&am2_origstate.region_open_head)
#undef ADDR_PATH_RESUME_HEAD
#define ADDR_PATH_RESUME_HEAD ((uintptr_t)(const void *)&am2_origstate.path_resume_head)
#undef ADDR_REGION_RESUME_GOALID
#define ADDR_REGION_RESUME_GOALID ((uintptr_t)(const void *)&am2_origstate.region_resume_goalid)
#undef ADDR_REGION_WALK
#define ADDR_REGION_WALK ((uintptr_t)(const void *)&am2_origstate.region_walk)
#undef ADDR_POINT_RULE_ARMY
#define ADDR_POINT_RULE_ARMY ((uintptr_t)(const void *)&am2_origstate.point_rule_army)
#undef ADDR_POINT_RULE
#define ADDR_POINT_RULE ((uintptr_t)(const void *)&am2_origstate.point_rule)
#undef ADDR_TILE_RING8
#define ADDR_TILE_RING8 ((uintptr_t)(const void *)&am2_origstate.tile_ring8)
#undef ADDR_REGION_RESUME_NODE
#define ADDR_REGION_RESUME_NODE ((uintptr_t)(const void *)&am2_origstate.region_resume_node)
#undef ADDR_TILE_RING4
#define ADDR_TILE_RING4 ((uintptr_t)(const void *)&am2_origstate.tile_ring4)
#undef ADDR_PATH_RESUME_GOAL
#define ADDR_PATH_RESUME_GOAL ((uintptr_t)(const void *)&am2_origstate.path_resume_goal)
#undef ADDR_TILEMASK_NEIGHBOURS
#define ADDR_TILEMASK_NEIGHBOURS ((uintptr_t)(const void *)&am2_origstate.tilemask_neighbours)
#undef ADDR_PATH_NODES
#define ADDR_PATH_NODES ((uintptr_t)(const void *)&am2_origstate.path_nodes)
#undef ADDR_TILE_NEIGHBOURS
#define ADDR_TILE_NEIGHBOURS ((uintptr_t)(const void *)&am2_origstate.tile_neighbours)
#undef ADDR_REGION_CURRENT
#define ADDR_REGION_CURRENT ((uintptr_t)(const void *)&am2_origstate.region_current)
#undef ADDR_REGION_GENERATION
#define ADDR_REGION_GENERATION ((uintptr_t)(const void *)&am2_origstate.region_generation)
#undef ADDR_PATH_GENERATION
#define ADDR_PATH_GENERATION ((uintptr_t)(const void *)&am2_origstate.path_generation)
#undef ADDR_PATH_FRAME_MS
#define ADDR_PATH_FRAME_MS ((uintptr_t)(const void *)&am2_origstate.path_frame_ms)
#undef ADDR_PATH_FRAME_STAMP
#define ADDR_PATH_FRAME_STAMP ((uintptr_t)(const void *)&am2_origstate.path_frame_stamp)
#undef ADDR_MISSILE_ANIMS
#define ADDR_MISSILE_ANIMS ((uintptr_t)(const void *)&am2_origstate.missile_anims)
#undef ADDR_ROACH_ANIMS
#define ADDR_ROACH_ANIMS ((uintptr_t)(const void *)&am2_origstate.roach_anims)
#undef ADDR_ROACH_MASK_DIRECTIONS
#define ADDR_ROACH_MASK_DIRECTIONS ((uintptr_t)(const void *)&am2_origstate.roach_mask_directions)
#undef ADDR_ROACH_MARK
#define ADDR_ROACH_MARK ((uintptr_t)(const void *)&am2_origstate.roach_mark)
#undef ADDR_ROACH_MARK_STAMP
#define ADDR_ROACH_MARK_STAMP ((uintptr_t)(const void *)&am2_origstate.roach_mark_stamp)
#undef ADDR_SCENARIO_UNREAD
#define ADDR_SCENARIO_UNREAD ((uintptr_t)(const void *)&am2_origstate.scenario_unread)
#undef ADDR_SCENARIO_COUNT
#define ADDR_SCENARIO_COUNT ((uintptr_t)(const void *)&am2_origstate.scenario_count)
#undef ADDR_SCENARIOS
#define ADDR_SCENARIOS ((uintptr_t)(const void *)&am2_origstate.scenarios)
#undef ADDR_SYSTEM_PALETTE
#define ADDR_SYSTEM_PALETTE ((uintptr_t)(const void *)&am2_origstate.system_palette)
#undef ADDR_MOVIE_CURRENT
#define ADDR_MOVIE_CURRENT ((uintptr_t)(const void *)&am2_origstate.movie_current)
#undef ADDR_MOVIE_SOUND_READY
#define ADDR_MOVIE_SOUND_READY ((uintptr_t)(const void *)&am2_origstate.movie_sound_ready)
#undef ADDR_SPRITE_FILE
#define ADDR_SPRITE_FILE ((uintptr_t)(const void *)&am2_origstate.sprite_file)
#undef ADDR_SPRITE_REG_CAP
#define ADDR_SPRITE_REG_CAP ((uintptr_t)(const void *)&am2_origstate.sprite_reg_cap)
#undef ADDR_SPRITE_REG_COUNT
#define ADDR_SPRITE_REG_COUNT ((uintptr_t)(const void *)&am2_origstate.sprite_reg_count)
#undef ADDR_SPRITE_TABLE
#define ADDR_SPRITE_TABLE ((uintptr_t)(const void *)&am2_origstate.sprite_table)
#undef ADDR_SPRITE_REG_PAIRS
#define ADDR_SPRITE_REG_PAIRS ((uintptr_t)(const void *)&am2_origstate.sprite_reg_pairs)
#undef ADDR_GLYPH_SIZE
#define ADDR_GLYPH_SIZE ((uintptr_t)(const void *)&am2_origstate.glyph_size)
#undef ADDR_GLYPH_OFFSETS
#define ADDR_GLYPH_OFFSETS ((uintptr_t)(const void *)&am2_origstate.glyph_offsets)
#undef ADDR_GLYPH_OFFSET_SPACE
#define ADDR_GLYPH_OFFSET_SPACE ((uintptr_t)(const void *)&am2_origstate.glyph_offset_space)
#undef ADDR_FONT_BASES
#define ADDR_FONT_BASES ((uintptr_t)(const void *)&am2_origstate.font_bases)
#undef ADDR_THROTTLE_DEADLINE
#define ADDR_THROTTLE_DEADLINE ((uintptr_t)(const void *)&am2_origstate.throttle_deadline)
#undef ADDR_SOLDIER_ANIMS
#define ADDR_SOLDIER_ANIMS ((uintptr_t)(const void *)&am2_origstate.soldier_anims)
#undef ADDR_TURN_REPEAT_MS
#define ADDR_TURN_REPEAT_MS ((uintptr_t)(const void *)&am2_origstate.turn_repeat_ms)
#undef ADDR_PENDING_CONFIRM
#define ADDR_PENDING_CONFIRM ((uintptr_t)(const void *)&am2_origstate.pending_confirm)
#undef ADDR_PAINT_OBJECT
#define ADDR_PAINT_OBJECT ((uintptr_t)(const void *)&am2_origstate.paint_object)
#undef ADDR_FOCUSED_EDIT
#define ADDR_FOCUSED_EDIT ((uintptr_t)(const void *)&am2_origstate.focused_edit)
#undef ADDR_MOVIE_PAGE
#define ADDR_MOVIE_PAGE ((uintptr_t)(const void *)&am2_origstate.movie_page)
#undef ADDR_VEHICLE_MASK_DIRECTIONS
#define ADDR_VEHICLE_MASK_DIRECTIONS ((uintptr_t)(const void *)&am2_origstate.vehicle_mask_directions)
#undef ADDR_OBJ_MARK
#define ADDR_OBJ_MARK ((uintptr_t)(const void *)&am2_origstate.obj_mark)
#undef ADDR_OBJ_MARK_STAMP
#define ADDR_OBJ_MARK_STAMP ((uintptr_t)(const void *)&am2_origstate.obj_mark_stamp)
#undef ADDR_VEHICLE_DEFS
#define ADDR_VEHICLE_DEFS ((uintptr_t)(const void *)&am2_origstate.vehicle_defs)
#undef ADDR_VEHICLE_DEF_COUNT
#define ADDR_VEHICLE_DEF_COUNT ((uintptr_t)(const void *)&am2_origstate.vehicle_def_count)
#undef ADDR_VEHICLE_DEF_CAP
#define ADDR_VEHICLE_DEF_CAP ((uintptr_t)(const void *)&am2_origstate.vehicle_def_cap)
#undef ADDR_SPAWN_KIND_TABLE
#define ADDR_SPAWN_KIND_TABLE ((uintptr_t)(const void *)&am2_origstate.spawn_kind_table)
#undef ADDR_UNUSED_662288
#define ADDR_UNUSED_662288 ((uintptr_t)(const void *)&am2_origstate.unused_662288)
#undef ADDR_SPAWN_EXTRA_6622BC
#define ADDR_SPAWN_EXTRA_6622BC ((uintptr_t)(const void *)&am2_origstate.spawn_extra_6622bc)
#undef ADDR_PICK_REACH_662450
#define ADDR_PICK_REACH_662450 ((uintptr_t)(const void *)&am2_origstate.pick_reach_662450)
#undef ADDR_PICK_REACH_6624EC
#define ADDR_PICK_REACH_6624EC ((uintptr_t)(const void *)&am2_origstate.pick_reach_6624ec)
#undef ADDR_MEDIC_HEAL_PCT
#define ADDR_MEDIC_HEAL_PCT ((uintptr_t)(const void *)&am2_origstate.medic_heal_pct)
#undef ADDR_PICK_REACH_66275C
#define ADDR_PICK_REACH_66275C ((uintptr_t)(const void *)&am2_origstate.pick_reach_66275c)
#undef ADDR_AIM_LIFE_HALF_A
#define ADDR_AIM_LIFE_HALF_A ((uintptr_t)(const void *)&am2_origstate.aim_life_half_a)
#undef ADDR_AIM_DAMAGE
#define ADDR_AIM_DAMAGE ((uintptr_t)(const void *)&am2_origstate.aim_damage)
#undef ADDR_AIM_LIFE_HALF_B
#define ADDR_AIM_LIFE_HALF_B ((uintptr_t)(const void *)&am2_origstate.aim_life_half_b)
#undef ADDR_AIM_SPAWN_ARG
#define ADDR_AIM_SPAWN_ARG ((uintptr_t)(const void *)&am2_origstate.aim_spawn_arg)
#undef ADDR_PICK_REACH_662894
#define ADDR_PICK_REACH_662894 ((uintptr_t)(const void *)&am2_origstate.pick_reach_662894)
#undef ADDR_REPAIR_HEAL_PCT
#define ADDR_REPAIR_HEAL_PCT ((uintptr_t)(const void *)&am2_origstate.repair_heal_pct)
#undef ADDR_PICK_REACH_6628C8
#define ADDR_PICK_REACH_6628C8 ((uintptr_t)(const void *)&am2_origstate.pick_reach_6628c8)
#undef ADDR_SPAWN_EXTRA_6628D4
#define ADDR_SPAWN_EXTRA_6628D4 ((uintptr_t)(const void *)&am2_origstate.spawn_extra_6628d4)
#undef ADDR_SWEEP_DISTANCE
#define ADDR_SWEEP_DISTANCE ((uintptr_t)(const void *)&am2_origstate.sweep_distance)
#undef ADDR_SWEEP_DAMAGE
#define ADDR_SWEEP_DAMAGE ((uintptr_t)(const void *)&am2_origstate.sweep_damage)
#undef ADDR_RESPAWN_KINDS
#define ADDR_RESPAWN_KINDS ((uintptr_t)(const void *)&am2_origstate.respawn_kinds)
#undef ADDR_RESPAWN_KIND_COUNT
#define ADDR_RESPAWN_KIND_COUNT ((uintptr_t)(const void *)&am2_origstate.respawn_kind_count)
#undef ADDR_DEF_MISSILE_RECS
#define ADDR_DEF_MISSILE_RECS ((uintptr_t)(const void *)&am2_origstate.def_missile_recs)
#undef ADDR_DEF_MISSILE_COUNT
#define ADDR_DEF_MISSILE_COUNT ((uintptr_t)(const void *)&am2_origstate.def_missile_count)
#undef ADDR_DEF_MISSILE_CAP
#define ADDR_DEF_MISSILE_CAP ((uintptr_t)(const void *)&am2_origstate.def_missile_cap)
#undef ADDR_ROWPOOL_A_COUNT
#define ADDR_ROWPOOL_A_COUNT ((uintptr_t)(const void *)&am2_origstate.rowpool_a_count)
#undef ADDR_ROWPOOL_A_TAIL
#define ADDR_ROWPOOL_A_TAIL ((uintptr_t)(const void *)&am2_origstate.rowpool_a_tail)
#undef ADDR_ROWPOOL_A_ENTRIES
#define ADDR_ROWPOOL_A_ENTRIES ((uintptr_t)(const void *)&am2_origstate.rowpool_a_entries)
#undef ADDR_SPAWN_KIND_TABLE_END
#define ADDR_SPAWN_KIND_TABLE_END ((uintptr_t)(const void *)&am2_origstate.spawn_kind_table_end)
#undef ADDR_SEQ_CTX_B
#define ADDR_SEQ_CTX_B ((uintptr_t)(const void *)&am2_origstate.seq_ctx_b)
#undef ADDR_ROWPOOL_B_COUNT
#define ADDR_ROWPOOL_B_COUNT ((uintptr_t)(const void *)&am2_origstate.rowpool_b_count)
#undef ADDR_ROWPOOL_B_TAIL
#define ADDR_ROWPOOL_B_TAIL ((uintptr_t)(const void *)&am2_origstate.rowpool_b_tail)
#undef ADDR_ROWPOOL_B_ENTRIES
#define ADDR_ROWPOOL_B_ENTRIES ((uintptr_t)(const void *)&am2_origstate.rowpool_b_entries)
#undef ADDR_SEQ_CTX_A
#define ADDR_SEQ_CTX_A ((uintptr_t)(const void *)&am2_origstate.seq_ctx_a)
#undef ADDR_MP_MARK_A
#define ADDR_MP_MARK_A ((uintptr_t)(const void *)&am2_origstate.mp_mark_a)
#undef ADDR_MP_MARK_B
#define ADDR_MP_MARK_B ((uintptr_t)(const void *)&am2_origstate.mp_mark_b)
#undef ADDR_MP_MARK_C
#define ADDR_MP_MARK_C ((uintptr_t)(const void *)&am2_origstate.mp_mark_c)
#undef ADDR_MP_TEAM_SPRITES
#define ADDR_MP_TEAM_SPRITES ((uintptr_t)(const void *)&am2_origstate.mp_team_sprites)
#undef ADDR_STARTUP_COLOURS_B
#define ADDR_STARTUP_COLOURS_B ((uintptr_t)(const void *)&am2_origstate.startup_colours_b)

/* The comm message records named for kCommMessages (split out of the members
 * that used to swallow them). */
#undef ADDR_MSG_KIND01
#define ADDR_MSG_KIND01 ((uintptr_t)(const void *)&am2_origstate.msg_kind01)
#undef ADDR_MSG_KIND02
#define ADDR_MSG_KIND02 ((uintptr_t)(const void *)&am2_origstate.msg_kind02)
#undef ADDR_MSG_KIND04
#define ADDR_MSG_KIND04 ((uintptr_t)(const void *)&am2_origstate.msg_kind04)
#undef ADDR_MSG_KIND0C
#define ADDR_MSG_KIND0C ((uintptr_t)(const void *)&am2_origstate.msg_kind0c)
#undef ADDR_MSG_KIND0D
#define ADDR_MSG_KIND0D ((uintptr_t)(const void *)&am2_origstate.msg_kind0d)
#undef ADDR_MSG_KIND10
#define ADDR_MSG_KIND10 ((uintptr_t)(const void *)&am2_origstate.msg_kind10)
#undef ADDR_ARMY_PACKET_B
#define ADDR_ARMY_PACKET_B ((uintptr_t)(const void *)&am2_origstate.army_packet_b)

/* The CRT's working globals (0x006645AC..0x00665C50): the MSVC 6 runtime's
 * .bss, already reserved as members at the end of am2_origstate and reached
 * by number until now. Same addresses, named. */
#undef ADDR_CRT_ADJUST_FDIV
#define ADDR_CRT_ADJUST_FDIV ((uintptr_t)(const void *)am2_origstate.crt_adjust_fdiv)
#undef ADDR_CRT_TIME_DST_CACHE
#define ADDR_CRT_TIME_DST_CACHE ((uintptr_t)(const void *)am2_origstate.crt_time_dst_cache)
#undef ADDR_CRT_TIME_SYSTIME_CACHE
#define ADDR_CRT_TIME_SYSTIME_CACHE ((uintptr_t)(const void *)am2_origstate.crt_time_systime_cache)
#undef ADDR_CRT_STRTOK_NEXT
#define ADDR_CRT_STRTOK_NEXT ((uintptr_t)(const void *)am2_origstate.crt_strtok_next)
#undef ADDR_CRT_AENVPTR
#define ADDR_CRT_AENVPTR ((uintptr_t)(const void *)am2_origstate.crt_aenvptr)
#undef ADDR_CRT_ERROR_MODE
#define ADDR_CRT_ERROR_MODE ((uintptr_t)(const void *)am2_origstate.crt_error_mode)
#undef ADDR_CRT_CFTOG_PFLT
#define ADDR_CRT_CFTOG_PFLT ((uintptr_t)(const void *)am2_origstate.crt_cftog_pflt)
#undef ADDR_CRT_CFTOG_ACTIVE
#define ADDR_CRT_CFTOG_ACTIVE ((uintptr_t)(const void *)am2_origstate.crt_cftog_active)
#undef ADDR_CRT_CFTOG_MAGNITUDE
#define ADDR_CRT_CFTOG_MAGNITUDE ((uintptr_t)(const void *)am2_origstate.crt_cftog_magnitude)
#undef ADDR_CRT_CFTOG_EXPANSION
#define ADDR_CRT_CFTOG_EXPANSION ((uintptr_t)(const void *)am2_origstate.crt_cftog_expansion)
#undef ADDR_CRT_PNHHEAP
#define ADDR_CRT_PNHHEAP ((uintptr_t)(const void *)am2_origstate.crt_pnhheap)
#undef ADDR_CRT_NEWMODE
#define ADDR_CRT_NEWMODE ((uintptr_t)(const void *)am2_origstate.crt_newmode)
#undef ADDR_CRT_ERRNO
#define ADDR_CRT_ERRNO ((uintptr_t)(const void *)am2_origstate.crt_errno)
#undef ADDR_CRT_DOSERRNO
#define ADDR_CRT_DOSERRNO ((uintptr_t)(const void *)am2_origstate.crt_doserrno)
#undef ADDR_CRT_UMASKVAL
#define ADDR_CRT_UMASKVAL ((uintptr_t)(const void *)am2_origstate.crt_umaskval)
#undef ADDR_CRT_OSVER
#define ADDR_CRT_OSVER ((uintptr_t)(const void *)am2_origstate.crt_osver)
#undef ADDR_CRT_WINVER
#define ADDR_CRT_WINVER ((uintptr_t)(const void *)am2_origstate.crt_winver)
#undef ADDR_CRT_WINMAJOR
#define ADDR_CRT_WINMAJOR ((uintptr_t)(const void *)am2_origstate.crt_winmajor)
#undef ADDR_CRT_WINMINOR
#define ADDR_CRT_WINMINOR ((uintptr_t)(const void *)am2_origstate.crt_winminor)
#undef ADDR_CRT_ARGC
#define ADDR_CRT_ARGC ((uintptr_t)(const void *)am2_origstate.crt_argc)
#undef ADDR_CRT_ARGV
#define ADDR_CRT_ARGV ((uintptr_t)(const void *)am2_origstate.crt_argv)
#undef ADDR_CRT_ENVIRON
#define ADDR_CRT_ENVIRON ((uintptr_t)(const void *)am2_origstate.crt_environ)
#undef ADDR_CRT_WENVIRON
#define ADDR_CRT_WENVIRON ((uintptr_t)(const void *)am2_origstate.crt_wenviron)
#undef ADDR_CRT_PGMPTR
#define ADDR_CRT_PGMPTR ((uintptr_t)(const void *)am2_origstate.crt_pgmptr)
#undef ADDR_CRT_EXIT_RETCALLER
#define ADDR_CRT_EXIT_RETCALLER ((uintptr_t)(const void *)am2_origstate.crt_exit_retcaller)
#undef ADDR_CRT_EXIT_STARTED
#define ADDR_CRT_EXIT_STARTED ((uintptr_t)(const void *)am2_origstate.crt_exit_started)
#undef ADDR_CRT_EXIT_DONE
#define ADDR_CRT_EXIT_DONE ((uintptr_t)(const void *)am2_origstate.crt_exit_done)
#undef ADDR_CRT_LC_HANDLE_CTYPE
#define ADDR_CRT_LC_HANDLE_CTYPE ((uintptr_t)(const void *)am2_origstate.crt_lc_handle_ctype)
#undef ADDR_CRT_LC_CODEPAGE
#define ADDR_CRT_LC_CODEPAGE ((uintptr_t)(const void *)am2_origstate.crt_lc_codepage)
#undef ADDR_CRT_CFLUSH
#define ADDR_CRT_CFLUSH ((uintptr_t)(const void *)am2_origstate.crt_cflush)
#undef ADDR_CRT_PGMNAME
#define ADDR_CRT_PGMNAME ((uintptr_t)(const void *)am2_origstate.crt_pgmname)
#undef ADDR_CRT_ENV_KIND
#define ADDR_CRT_ENV_KIND ((uintptr_t)(const void *)am2_origstate.crt_env_kind)
#undef ADDR_CRT_MSGBANNER_HOOK
#define ADDR_CRT_MSGBANNER_HOOK ((uintptr_t)(const void *)am2_origstate.crt_msgbanner_hook)
#undef ADDR_CRT_FLTOUT_RAW
#define ADDR_CRT_FLTOUT_RAW ((uintptr_t)(const void *)am2_origstate.crt_fltout_raw)
#undef ADDR_CRT_STRFLT
#define ADDR_CRT_STRFLT ((uintptr_t)(const void *)am2_origstate.crt_strflt)
#undef ADDR_CRT_COMMODE
#define ADDR_CRT_COMMODE ((uintptr_t)(const void *)am2_origstate.crt_commode)
#undef ADDR_CRT_SEH_OLD_FILTER
#define ADDR_CRT_SEH_OLD_FILTER ((uintptr_t)(const void *)am2_origstate.crt_seh_old_filter)
#undef ADDR_CRT_TZ_API_USED
#define ADDR_CRT_TZ_API_USED ((uintptr_t)(const void *)am2_origstate.crt_tz_api_used)
#undef ADDR_CRT_TZ_INFO
#define ADDR_CRT_TZ_INFO ((uintptr_t)(const void *)am2_origstate.crt_tz_info)
#undef ADDR_CRT_LAST_TZ
#define ADDR_CRT_LAST_TZ ((uintptr_t)(const void *)am2_origstate.crt_last_tz)
#undef ADDR_CRT_TZSET_DONE
#define ADDR_CRT_TZSET_DONE ((uintptr_t)(const void *)am2_origstate.crt_tzset_done)
#undef ADDR_CRT_MBCP_FROM_SYSTEM
#define ADDR_CRT_MBCP_FROM_SYSTEM ((uintptr_t)(const void *)am2_origstate.crt_mbcp_from_system)
#undef ADDR_CRT_MSGBOX_FN
#define ADDR_CRT_MSGBOX_FN ((uintptr_t)(const void *)am2_origstate.crt_msgbox_fn)
#undef ADDR_CRT_GETACTIVEWINDOW_FN
#define ADDR_CRT_GETACTIVEWINDOW_FN ((uintptr_t)(const void *)am2_origstate.crt_getactivewindow_fn)
#undef ADDR_CRT_GETLASTACTIVEPOPUP_FN
#define ADDR_CRT_GETLASTACTIVEPOPUP_FN ((uintptr_t)(const void *)am2_origstate.crt_getlastactivepopup_fn)
#undef ADDR_CRT_FMODE
#define ADDR_CRT_FMODE ((uintptr_t)(const void *)am2_origstate.crt_fmode)
#undef ADDR_CRT_MBCODEPAGE
#define ADDR_CRT_MBCODEPAGE ((uintptr_t)(const void *)am2_origstate.crt_mbcodepage)
#undef ADDR_CRT_MBULINFO
#define ADDR_CRT_MBULINFO ((uintptr_t)(const void *)am2_origstate.crt_mbulinfo)
#undef ADDR_CRT_ISMBCODEPAGE
#define ADDR_CRT_ISMBCODEPAGE ((uintptr_t)(const void *)am2_origstate.crt_ismbcodepage)
#undef ADDR_CRT_MBCASEMAP
#define ADDR_CRT_MBCASEMAP ((uintptr_t)(const void *)am2_origstate.crt_mbcasemap)
#undef ADDR_CRT_MBCTYPE
#define ADDR_CRT_MBCTYPE ((uintptr_t)(const void *)am2_origstate.crt_mbctype)
#undef ADDR_CRT_MBLCID
#define ADDR_CRT_MBLCID ((uintptr_t)(const void *)am2_origstate.crt_mblcid)
#undef ADDR_CRT_PIOB
#define ADDR_CRT_PIOB ((uintptr_t)(const void *)am2_origstate.crt_piob)
#undef ADDR_CRT_NSTREAM
#define ADDR_CRT_NSTREAM ((uintptr_t)(const void *)am2_origstate.crt_nstream)
#undef ADDR_CRT_PIOINFO
#define ADDR_CRT_PIOINFO ((uintptr_t)(const void *)am2_origstate.crt_pioinfo)
#undef ADDR_CRT_NHANDLE
#define ADDR_CRT_NHANDLE ((uintptr_t)(const void *)am2_origstate.crt_nhandle)
#undef ADDR_CRT_ENV_INITIALIZED
#define ADDR_CRT_ENV_INITIALIZED ((uintptr_t)(const void *)am2_origstate.crt_env_initialized)
#undef ADDR_CRT_MBCTABLE_INIT
#define ADDR_CRT_MBCTABLE_INIT ((uintptr_t)(const void *)am2_origstate.crt_mbctable_init)
#undef ADDR_CRT_ONEXITEND
#define ADDR_CRT_ONEXITEND ((uintptr_t)(const void *)am2_origstate.crt_onexitend)
#undef ADDR_CRT_ONEXITBEGIN
#define ADDR_CRT_ONEXITBEGIN ((uintptr_t)(const void *)am2_origstate.crt_onexitbegin)
#undef ADDR_CRT_SBH_SIZE_HEADER_LIST
#define ADDR_CRT_SBH_SIZE_HEADER_LIST ((uintptr_t)(const void *)am2_origstate.crt_sbh_size_header_list)
#undef ADDR_CRT_SBH_IND_GROUP_DEFER
#define ADDR_CRT_SBH_IND_GROUP_DEFER ((uintptr_t)(const void *)am2_origstate.crt_sbh_ind_group_defer)
#undef ADDR_CRT_SBH_PHEADER_SCAN
#define ADDR_CRT_SBH_PHEADER_SCAN ((uintptr_t)(const void *)am2_origstate.crt_sbh_pheader_scan)
#undef ADDR_CRT_SBH_PHEADER_DEFER
#define ADDR_CRT_SBH_PHEADER_DEFER ((uintptr_t)(const void *)am2_origstate.crt_sbh_pheader_defer)
#undef ADDR_CRT_SBH_CNT_HEADER_LIST
#define ADDR_CRT_SBH_CNT_HEADER_LIST ((uintptr_t)(const void *)am2_origstate.crt_sbh_cnt_header_list)
#undef ADDR_CRT_SBH_PHEADER_LIST
#define ADDR_CRT_SBH_PHEADER_LIST ((uintptr_t)(const void *)am2_origstate.crt_sbh_pheader_list)
#undef ADDR_CRT_CRTHEAP
#define ADDR_CRT_CRTHEAP ((uintptr_t)(const void *)am2_origstate.crt_crtheap)
#undef ADDR_CRT_ACMDLN
#define ADDR_CRT_ACMDLN ((uintptr_t)(const void *)am2_origstate.crt_acmdln)


/* The relink build (VC6, AM2_RELINK) re-points every am2_origstate redirect
 * above at a per-global symbol, so each reads as the original's lone global;
 * see src/vc6/relinkos.py. Only VC6 compiles have src/vc6/include on the path. */
#if defined(AM2_RELINK)
#include <am2_relink_os.h>
#endif

#endif /* AM2_STANDALONE_H */
