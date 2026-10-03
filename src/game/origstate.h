/* origstate.h -- the AM2_OrigState layout (extracted from origstate.cpp so the
 * standalone build's ADDR redirects can name its members).  Definition only;
 * origstate.cpp defines the instance and pins every offset with static_asserts. */
#ifndef AM2_ORIGSTATE_H
#define AM2_ORIGSTATE_H

#include <stdint.h>
#include <stddef.h>
#include "globaltypes.h"

struct AM2_OS_DepthNode {          /* ADDR_DEPTH_NODES, 12 bytes, {obj,prev,next} */
    void *obj, *prev, *next;
};
struct AM2_OS_Timer {              /* ADDR_TIMER_TABLE, 16 bytes */
    uint32_t start;             /* mission ms of the next fire */
    uint32_t period;            /* ms between fires */
    int32_t  count;             /* fires remaining */
    int32_t  id;                /* 0 = free slot */
};
struct AM2_OS_PadNumber {          /* ADDR_PAD_NUMBERS, 76 bytes */
    int16_t count;              /* +0x00 */
    int16_t pads[32];           /* +0x02, indices into ADDR_PADS */
    int16_t cx, cy;             /* +0x42, +0x44, sixteenths of a cell */
    uint8_t rest[76 - 0x46];
};
struct AM2_OS_Pad {                /* ADDR_PADS, 72 bytes (script.h's AM2_OS_Pad) */
    int32_t id, number, name, compared, specific, trigger, compare, threshold;
    int32_t delay0, delay1;     /* +0x00..+0x24, written by the parser */
    uint8_t rest[0x38 - 0x28];  /* +0x28 the event key, written via event.cpp */
    int32_t period;             /* +0x38, repeat period ms */
    int32_t dueAt;              /* +0x3C, game-clock ms */
    uint8_t rest40[72 - 0x40];
};
struct AM2_OS_PlayerRecord {       /* ADDR_PLAYER_RECORDS, 0x7E0 = 2016 bytes, six of them */
    uint8_t  head[0x14];        /* +0x00 */
    uint32_t own_bit;           /* +0x14, 1 << slot */
    uint32_t want_bit;          /* +0x18, 1 << (slot + AM2_PLAYER_WANT_SHIFT) */
    uint8_t  mid1[0x5C - 0x1C]; /* +0x1C */
    uint32_t made_at;           /* +0x5C, GetTickCount when the record was made */
    uint8_t  mid2[0x78 - 0x60]; /* +0x60 (includes +0x70 last-seen tick) */
    uint8_t  msgs[0x88 - 0x78]; /* +0x78, the list ADDR_MSG_LIST_INIT takes */
    uint32_t is_host;           /* +0x88 */
    uint8_t  mid3[0x94 - 0x8C]; /* +0x8C */
    uint32_t flag_94;           /* +0x94, written 1, never read */
    uint8_t  mid4[0x420 - 0x98];/* +0x98 */
    int32_t  ring_a[120];       /* +0x420, PLAYER_REC_OFF_RING_A */
    int32_t  ring_b[120];       /* +0x600, PLAYER_REC_OFF_RING_B */
};
struct AM2_OS_PacketRecord {       /* ADDR_PACKET_RECORDS, 0x28 = 40 bytes, 400 of them */
    uint8_t  head[0x10];        /* +0x00 (len@4, seq@8, checksum@0xC on the wire view) */
    uint32_t size;              /* +0x10, PACKET_REC_OFF_SIZE */
    uint32_t key;               /* +0x14, PACKET_REC_OFF_KEY */
    uint32_t flags;             /* +0x18, PACKET_REC_OFF_FLAGS */
    uint8_t  mid[0x20 - 0x1C];  /* +0x1C */
    void    *data;              /* +0x20, PACKET_REC_OFF_DATA -> a 0x400 buffer */
    uint8_t  tail[0x28 - 0x24]; /* +0x24 */
};
struct AM2_OS_RowPoolEntry {       /* AM2_ROWPOOL_ENTRY_BYTES = 12 */
    void    *row;               /* +0x00, ROWPOOL_OFF_ROW -> a 0x60 row block */
    uint16_t id;                /* +0x04, ROWPOOL_OFF_ID */
    uint16_t prev;              /* +0x06, ROWPOOL_OFF_PREV (slot index) */
    uint16_t next;              /* +0x08, ROWPOOL_OFF_NEXT (slot index) */
    uint16_t _pad;              /* +0x0A */
};
struct AM2_OS_SoundSlot {          /* SOUND_SLOT_STRIDE = 16, 56 fixed slots */
    void    *buffer;            /* +0x00, SOUND_SLOT_OFF_BUFFER (a DirectSound buffer;
                                 *  void* here -- this flat file names no COM type) */
    uint32_t bytes;             /* +0x04, SOUND_SLOT_OFF_BYTES */
    int32_t  volume;            /* +0x08, SOUND_SLOT_OFF_VOLUME */
    uint32_t started;           /* +0x0C, SOUND_SLOT_OFF_STARTED */
};
struct AM2_OS_SightDir {           /* AM2_SIGHT_DIR_STRIDE = 16, record[64] by bearing */
    int16_t low;                /* +0x00, SIGHTDIR_OFF_LOW */
    int16_t mid;                /* +0x02, SIGHTDIR_OFF_MID */
    int16_t high;               /* +0x04, SIGHTDIR_OFF_HIGH */
    int16_t _pad;               /* +0x06 */
    int32_t stamp;              /* +0x08, SIGHTDIR_OFF_STAMP (minima current) */
    int32_t trace_stamp;        /* +0x0C, SIGHTDIR_OFF_TRACE_STAMP (line walked) */
};

struct AM2_OrigState {
    uint8_t _pad_head[407928];
    uint8_t packet_buffers_end[8];  /* 0x004F1978 */
    struct AM2_OS_PlayerRecord player_records[6];  /* 0x004F1980 */
    uint8_t default_player_evt[8];  /* 0x004F48C0 */
    uint8_t msg_list_b[16];  /* 0x004F48C8 */
    uint8_t packet_thread[4];  /* 0x004F48D8 */
    uint8_t player_slot_mask[4];  /* 0x004F48DC */
    uint8_t comm_no_buffers_latch[8];  /* 0x004F48E0 */
    uint8_t msg_kind02[8];  /* 0x004F48E8 */
    uint8_t last_msg_value[4];  /* 0x004F48F0 */
    uint8_t last_msg_checksum[4];  /* 0x004F48F4 */
    struct AM2_OS_PacketRecord packet_records[400];  /* 0x004F48F8 */
    uint8_t exit_game_flag[4];  /* 0x004F8778 */
    uint8_t packet_state[4];  /* 0x004F877C */
    uint8_t msg_list_delayed[16];  /* 0x004F8780 */
    uint8_t recv_scratch[1024];  /* 0x004F8790 */
    uint8_t packet_thread_id[8];  /* 0x004F8B90 */
    uint8_t msg_wanted_flags[16];  /* 0x004F8B98 */
    uint8_t send_roll[1040];  /* 0x004F8BA8 */
    struct AM2_OS_SightDir sight_block_by_dir[64];  /* 0x004F8FB8 */
    uint8_t perframe_count_a[4];  /* 0x004F93B8 */
    uint8_t perframe_count_b[4];  /* 0x004F93BC */
    uint8_t sight_generation[8];  /* 0x004F93C0 */
    uint8_t air_leg1_dy[4];  /* 0x004F93C8 */
    uint8_t air_leg2_divisor[4];  /* 0x004F93CC */
    uint8_t air_leg2_ms_span[4];  /* 0x004F93D0 */
    uint8_t air_leg3_dx[4];  /* 0x004F93D4 */
    uint8_t air_sprites_2[4];  /* 0x004F93D8 */
    void   *air_sprites_3[20];  /* 0x004F93DC */
    void   *air_sprites_6[11];  /* 0x004F942C */
    uint8_t air_sprites_edge[4];  /* 0x004F9458 */
    uint8_t air_save_block[584];  /* 0x004F945C */
    uint8_t air_leg2_dy[4];  /* 0x004F96A4 */
    uint8_t air_leg3_dy[4];  /* 0x004F96A8 */
    uint8_t air_leg1_x0[4];  /* 0x004F96AC */
    uint8_t air_leg1_dx[4];  /* 0x004F96B0 */
    uint8_t air_leg3_x1[4];  /* 0x004F96B4 */
    uint8_t dir_scratch[8];  /* 0x004F96B8 */
    uint8_t sprite_list_cap[4];  /* 0x004F96C0 */
    uint8_t sprite_list_count[4];  /* 0x004F96C4 */
    uint8_t sprite_list[4];  /* 0x004F96C8 */
    uint8_t army_ramp_tables[4][0x100];  /* 0x004F96CC */
    uint8_t obj_table_records[4];  /* 0x004F9ACC */
    uint8_t chat_colour_table[1020];  /* 0x004F9AD0 */
    uint8_t army_obj_lists[4];  /* 0x004F9ECC */
    uint8_t type2_action_list[12];  /* 0x004F9ED0 */
    uint8_t row_lut_doubles[256];  /* 0x004F9EDC */
    uint8_t default_owner[4];  /* 0x004F9FDC */
    uint8_t wincpuid_fn[4];  /* 0x004F9FE0 */
    uint8_t last_message[4];  /* 0x004F9FE4 */
    uint8_t cpunormspeed_fn[4];  /* 0x004F9FE8 */
    uint8_t opt_map_name[64];  /* 0x004F9FEC */
    uint8_t app_active[4];  /* 0x004FA02C */
    uint8_t present_enabled[4];  /* 0x004FA030 */
    uint8_t app_mutex[4];  /* 0x004FA034 */
    uint8_t opt_no_intro[8];  /* 0x004FA038 */
    struct AM2_OS_SoundSlot sound_slots[56];  /* 0x004FA040 .. 0x004FA3C0 */
    uint8_t sound_slots_end[64];  /* 0x004FA3C0 */
    uint8_t sound_dynamic_last[4];  /* 0x004FA400 */
    uint8_t audio_buffer[4];  /* 0x004FA404 */
    uint8_t audio_timer_id[8];  /* 0x004FA408 */
    uint8_t audio_waveformat[4];  /* 0x004FA410 */
    uint8_t audio_hmmio[4];  /* 0x004FA414 */
    uint8_t audio_data_chunk[20];  /* 0x004FA418 */
    uint8_t audio_riff_chunk[20];  /* 0x004FA42C */
    uint8_t audio_buffer_2[4];  /* 0x004FA440 */
    uint8_t audio_buffer_size[4];  /* 0x004FA444 */
    uint8_t audio_period[4];  /* 0x004FA448 */
    uint8_t audio_cursor_b[4];  /* 0x004FA44C */
    uint8_t audio_cursor_a[4];  /* 0x004FA450 */
    uint8_t audio_valid_bytes[4];  /* 0x004FA454 */
    uint8_t audio_read_failed[4];  /* 0x004FA458 */
    uint8_t audio_looping[4];  /* 0x004FA45C */
    uint8_t audio_at_end[4];  /* 0x004FA460 */
    uint8_t audio_timer_run[4];  /* 0x004FA464 */
    uint8_t audio_enabled[4];  /* 0x004FA468 */
    uint8_t dsound[4];  /* 0x004FA46C */
    uint8_t ds_primary[4];  /* 0x004FA470 */
    uint8_t ds_listener[4];  /* 0x004FA474 */
    uint8_t audio_in_callback[8];  /* 0x004FA478 */
    uint8_t comm_global[1152];  /* 0x004FA480 */
    uint8_t connection_list[4];  /* 0x004FA900 */
    uint8_t our_slot[4];  /* 0x004FA904 */
    uint8_t session_list[8];  /* 0x004FA908 */
    uint8_t msg_chat[264];  /* 0x004FA910 */
    uint8_t msg_ready_to_load[16];  /* 0x004FAA18 */
    uint8_t msg_game_ready[16];  /* 0x004FAA28 */
    uint8_t msg_kind10[24];  /* 0x004FAA38 */
    uint8_t msg_game_pause[24];  /* 0x004FAA50 */
    uint8_t army_packet[4];  /* 0x004FAA68 */
    uint8_t army_packet_len[4];  /* 0x004FAA6C */
    uint8_t army_packet_seq[1016];  /* 0x004FAA70 */
    uint8_t resend_buf[1032];  /* 0x004FAE68 */
    uint8_t army_packet_b[1280];  /* 0x004FB270 */
    uint8_t msg_map[16];  /* 0x004FB770 */
    uint8_t resend_scratch[1024];  /* 0x004FB780 */
    uint8_t msg_kind04[1024];  /* 0x004FBB80 */
    uint8_t msg_kind01[1032];  /* 0x004FBF80 */
    uint8_t msg_kind0c[32];  /* 0x004FC388 */
    uint8_t msg_end_setup[16];  /* 0x004FC3A8 */
    uint8_t player_msg[568];  /* 0x004FC3B8 */
    uint8_t msg_game_start[400];  /* 0x004FC5F0 */
    uint8_t game_seed_sent[8];  /* 0x004FC780 */
    uint8_t msg_kind0d[272];  /* 0x004FC788 */
    uint8_t msg_color[16];  /* 0x004FC898 */
    uint8_t msg_team[12];  /* 0x004FC8A8 */
    uint8_t data_checksum[4];  /* 0x004FC8B4 */
    uint8_t timeout_logged_at[4];  /* 0x004FC8B8 */
    uint8_t change_limit_floor[4];  /* 0x004FC8BC */
    uint8_t bandwidth_last_seq[8];  /* 0x004FC8C0 */
    void   *aim_sprites_a[6];  /* 0x004FC8C8 */
    int32_t aim_live_a[4];  /* 0x004FC8E0 */
    uint8_t aim_point_a[16];  /* 0x004FC8F0 */
    int32_t aim_stamp_a[4];  /* 0x004FC900 */
    int32_t aim_deadline_a[4];  /* 0x004FC910 */
    void   *aim_sprites_b[9];  /* 0x004FC920 */
    int32_t aim_live_b[4];  /* 0x004FC944 */
    uint8_t aim_point_b[16];  /* 0x004FC954 */
    int32_t aim_frame_b[4];  /* 0x004FC964 */
    int32_t aim_stamp_b[4];  /* 0x004FC974 */
    uint8_t aim_deadline_b[20];  /* 0x004FC984 */
    uint8_t palette_glyphs[256];  /* 0x004FC998 */
    uint8_t menu_saved_rect[16];  /* 0x004FCA98 */
    uint8_t menu_row[4];  /* 0x004FCAA8 */
    void   *menu_sprites[190];  /* 0x004FCAAC, AM2_Sprite*[190] (void* here) */
    uint8_t menu_sprites_end[4];  /* 0x004FCDA4 */
    uint8_t menu_overlay_a[4];  /* 0x004FCDA8 */
    uint8_t menu_overlay_b[4];  /* 0x004FCDAC */
    uint8_t menu_ink[4];  /* 0x004FCDB0 */
    uint8_t menu_overlay_a_ink[4];  /* 0x004FCDB4 */
    uint8_t menu_overlay_b_ink[4];  /* 0x004FCDB8 */
    uint8_t menu_cursor_dx[4];  /* 0x004FCDBC */
    uint8_t menu_overlay_a_dx[4];  /* 0x004FCDC0 */
    uint8_t menu_overlay_b_dx[4];  /* 0x004FCDC4 */
    uint8_t menu_cursor_prev[16];  /* 0x004FCDC8 */
    uint8_t menu_cursor_rect[16];  /* 0x004FCDD8 */
    uint8_t menu_anim_next[4];  /* 0x004FCDE8 */
    uint8_t menu_anim_frame[4];  /* 0x004FCDEC */
    uint8_t menu_row_stamp[4];  /* 0x004FCDF0 */
    uint8_t menu_surface[4];  /* 0x004FCDF4 */
    uint8_t flame_record[256];  /* 0x004FCDF8 */
    uint8_t menu_enabled[4];  /* 0x004FCEF8 */
    uint8_t menu_saved_valid[4];  /* 0x004FCEFC */
    uint8_t hud_widget_a[4];  /* 0x004FCF00 */
    uint8_t hud_widget_table[72];  /* 0x004FCF04 */
    uint8_t hud_widget_c[4];  /* 0x004FCF4C */
    uint8_t hud_index[4];  /* 0x004FCF50 */
    uint8_t hud_widget_b[4];  /* 0x004FCF54 */
    uint8_t view_rect_on[4];  /* 0x004FCF58 */
    uint8_t radar_colours[12];  /* 0x004FCF5C */
    uint8_t drag_anchor[8];  /* 0x004FCF68 */
    uint8_t view_rect[16];  /* 0x004FCF70 */
    uint8_t pointer_mode[4];  /* 0x004FCF80 */
    uint8_t hud_dirty[4];  /* 0x004FCF84 */
    uint8_t place_flag_4fcf88[4];  /* 0x004FCF88 */
    uint8_t place_facing[4];  /* 0x004FCF8C */
    uint8_t number_key_slot[4];  /* 0x004FCF90 */
    uint8_t cheat_enabled[4];  /* 0x004FCF94 */
    uint8_t cheat_level_select[4];  /* 0x004FCF98 */
    uint8_t our_points[4];  /* 0x004FCF9C */
    uint8_t flame_on[4];  /* 0x004FCFA0 */
    uint8_t flame_next_ms[4];  /* 0x004FCFA4 */
    uint8_t view_colour_copy[1940];  /* 0x004FCFA8 */
    uint8_t opt_rob[4];  /* 0x004FD73C */
    uint8_t opt_dan[4];  /* 0x004FD740 */
    uint8_t opt_peter[4];  /* 0x004FD744 */
    uint8_t opt_4fd748[24];  /* 0x004FD748 */
    uint8_t colour_dark_blue[4];  /* 0x004FD760 */
    uint8_t remap_identity[4];  /* 0x004FD764 */
    uint8_t colour_white[8];  /* 0x004FD768 */
    uint8_t active_palette_buf[2052];  /* 0x004FD770 */
    uint8_t colour_dark_green[1];  /* 0x004FDF74 */
    uint8_t colour_cream[3];  /* 0x004FDF75 */
    uint8_t directdraw[4];  /* 0x004FDF78 */
    uint8_t colour_blue[4];  /* 0x004FDF7C */
    uint8_t surface_locked[260];  /* 0x004FDF80 */
    uint8_t default_palette[4];  /* 0x004FE084 */
    uint8_t colour_steel_blue[1];  /* 0x004FE088 */
    uint8_t view_rect_colour[3];  /* 0x004FE089 */
    uint8_t back_buffer[4];  /* 0x004FE08C */
    uint8_t colour_stale[1];  /* 0x004FE090 */
    uint8_t colour_light_green[1];  /* 0x004FE091 */
    uint8_t colour_lag_mid[1];  /* 0x004FE092 */
    uint8_t colour_light_grey[1];  /* 0x004FE093 */
    uint8_t colour_black[4];  /* 0x004FE094 */
    uint8_t directdraw2[4];  /* 0x004FE098 */
    uint8_t remap_bright_buf[264];  /* 0x004FE09C */
    uint8_t overlay_palette[4];  /* 0x004FE1A4 */
    uint8_t framebuffer[4];  /* 0x004FE1A8 */
    uint8_t colour_light_blue[1];  /* 0x004FE1AC */
    uint8_t colour_white_b[1];  /* 0x004FE1AD */
    uint8_t colour_olive[2];  /* 0x004FE1AE */
    uint8_t remap_tint_buf[256];  /* 0x004FE1B0 */
    uint8_t *remap_shades[4];  /* 0x004FE2B0 */
    uint8_t variation_blocks[64][256];  /* 0x004FE2C0 */
    uint8_t variation_end[8];  /* 0x005022C0 */
    uint8_t palette_copy[2052];  /* 0x005022C8 */
    uint8_t list_ink_hot_sel[4];  /* 0x00502ACC */
    uint8_t screen_pitch[4];  /* 0x00502AD0 */
    uint8_t primary_surface[4];  /* 0x00502AD4 */
    uint8_t colour_below_bg[1];  /* 0x00502AD8 */
    uint8_t background_colour[259];  /* 0x00502AD9 */
    uint8_t default_palette_buf[264];  /* 0x00502BDC */
    uint8_t colour_dark_grey[1];  /* 0x00502CE4 */
    uint8_t colour_no_map[7];  /* 0x00502CE5 */
    uint8_t remap_shade_store[1044];  /* 0x00502CEC */
    uint8_t offscreen_surface[8];  /* 0x00503100 */
    uint32_t tileset_palettes[8][513];  /* 0x00503108, PALETTEENTRY[8][513] */
    uint8_t draw_target[4];  /* 0x00507128 */
    uint8_t kind7_names[4];  /* 0x0050712C */
    uint8_t *variation_table[64];  /* 0x00507130 */
    uint8_t remap_bright[4];  /* 0x00507230 */
    uint8_t hud_message_colour[4];  /* 0x00507234 */
    uint8_t remap_identity_buf[264];  /* 0x00507238 */
    uint8_t dd_clipper[4];  /* 0x00507340 */
    uint8_t opt_windowed[4];  /* 0x00507344 */
    uint8_t depth_cursor[8];  /* 0x00507348 */
    struct AM2_OS_DepthNode depth_nodes[500];  /* 0x00507350 */
    uint8_t dirty_tail[4];  /* 0x00508AC0 */
    uint8_t dirty_rects[16];  /* 0x00508AC4 */
    uint8_t dirty_prev_head[2];  /* 0x00508AD4 */
    uint8_t dirty_head[9982];  /* 0x00508AD6 */
    uint8_t depth_count[4];  /* 0x0050B1D4 */
    uint8_t depth_head[4];  /* 0x0050B1D8 */
    uint8_t depth_field_dc[4];  /* 0x0050B1DC */
    uint8_t error_text_dd[1024];  /* 0x0050B1E0 */
    uint8_t error_text[3424];  /* 0x0050B5E0 */
    uint8_t leak_total[4];  /* 0x0050C340 */
    uint8_t leak_count[4];  /* 0x0050C344 */
    uint8_t leak_records[4];  /* 0x0050C348 */
    uint8_t unread_50c34c[4];  /* 0x0050C34C */
    uint8_t script_reloading[4];  /* 0x0050C350 */
    uint8_t opt_trace_win[4];  /* 0x0050C354 */
    uint8_t opt_dbg[4];  /* 0x0050C358 */
    uint8_t opt_trace_pf[4];  /* 0x0050C35C */
    uint8_t opt_trace_veh[8];  /* 0x0050C360 */
    uint8_t event_block[4];  /* 0x0050C368 */
    uint8_t timer_count[4];  /* 0x0050C36C */
    struct AM2_OS_Timer timer_table[1000];  /* 0x0050C370 */
    uint8_t event_table[12];  /* 0x005101F0 */
    uint8_t timer_table_id_end[24];  /* 0x005101FC */
    uint8_t script_conditions[4];  /* 0x00510214 */
    uint8_t rule_uid_a[4];  /* 0x00510218 */
    uint8_t rule_uid_b[4];  /* 0x0051021C */
    uint8_t rule_uid_c[8];  /* 0x00510220 */
    uint8_t explosion_anims[8];  /* 0x00510228 */
    uint8_t sprite_set_shared[2064];  /* 0x00510230 */
    uint8_t sprite_set_title[2064];  /* 0x00510A40 */
    uint8_t sprite_set_third[2064];  /* 0x00511250 */
    uint8_t state0_tick[8];  /* 0x00511A60 */
    uint8_t gameproc_block[32];  /* 0x00511A68 */
    uint8_t map_name[64];  /* 0x00511A88 */
    uint8_t map_folder[64];  /* 0x00511AC8 */
    uint8_t movie_to_play[128];  /* 0x00511B08 */
    uint8_t gameproc_str_b[64];  /* 0x00511B88 */
    uint8_t script_reload_path[64];  /* 0x00511BC8 */
    uint8_t mp_script_name[64];  /* 0x00511C08 */
    uint8_t level_str_a[64];  /* 0x00511C48 */
    uint8_t level_str_b[64];  /* 0x00511C88 */
    uint8_t tileset_reserve[4];  /* 0x00511CC8 */
    uint8_t map_checksum_val[4];  /* 0x00511CCC */
    uint8_t mp_script_chksum_val[4];  /* 0x00511CD0 */
    uint8_t rules_checksum_val[4];  /* 0x00511CD4 */
    uint8_t level_str_d[64];  /* 0x00511CD8 */
    uint8_t level_str_c[64];  /* 0x00511D18 */
    uint8_t level_sound_name[64];  /* 0x00511D58 */
    uint8_t level_id[4];  /* 0x00511D98 */
    uint8_t level_index[4];  /* 0x00511D9C */
    uint8_t mp_session[4];  /* 0x00511DA0 */
    uint8_t game_state[4];  /* 0x00511DA4 */
    uint8_t state_entered[4];  /* 0x00511DA8 */
    uint8_t state_pending[4];  /* 0x00511DAC */
    uint8_t state_wanted[4];  /* 0x00511DB0 */
    uint8_t game_state_arg[8];  /* 0x00511DB4 */
    uint8_t menu_mode[4];  /* 0x00511DBC */
    uint8_t overlay_dirty[4];  /* 0x00511DC0 */
    uint8_t menu_request_set[4];  /* 0x00511DC4 */
    uint8_t menu_request[4];  /* 0x00511DC8 */
    uint8_t script_reload[4];  /* 0x00511DCC */
    uint8_t state_enter_once[4];  /* 0x00511DD0 */
    uint8_t net_game[4];  /* 0x00511DD4 */
    uint8_t load_pending[4];  /* 0x00511DD8 */
    uint8_t have_default_cof[4];  /* 0x00511DDC */
    uint8_t uid_counters[20];  /* 0x00511DE0 */
    uint8_t next_uid[4];  /* 0x00511DF4 */
    uint8_t pad_count[4];  /* 0x00511DF8 */
    uint8_t script_state_flag[4];  /* 0x00511DFC */
    uint8_t clock_base_ms[4];  /* 0x00511E00 */
    uint8_t game_clock_ms[4];  /* 0x00511E04 */
    uint8_t frame_delta_ms[4];  /* 0x00511E08 */
    uint8_t last_tick_ms[4];  /* 0x00511E0C */
    uint8_t frame_delta_sec[4];  /* 0x00511E10 */
    int32_t game_over_source[3];  /* 0x00511E14 */
    uint8_t evt_id15_uid[4];  /* 0x00511E20 */
    uint8_t obj_ctx_val_a[4];  /* 0x00511E24 */
    uint8_t obj_ctx_val[4];  /* 0x00511E28 */
    uint8_t obj_ctx_val_prev[4];  /* 0x00511E2C */
    uint8_t level_flag_e30[4];  /* 0x00511E30 */
    uint8_t view_snap[4];  /* 0x00511E34 */
    uint8_t view_hold[4];  /* 0x00511E38 */
    uint8_t obj_ctx_set[4];  /* 0x00511E3C */
    uint8_t obj_ctx_set_prev[4];  /* 0x00511E40 */
    uint8_t input_suppress[4];  /* 0x00511E44 */
    uint8_t evt_id15_flag[4];  /* 0x00511E48 */
    uint8_t our_leader_uid[4];  /* 0x00511E4C */
    uint8_t leader_pos[4];  /* 0x00511E50 */
    uint8_t leader_facing[4];  /* 0x00511E54 */
    uint8_t weapon_owner_id[4];  /* 0x00511E58 */
    uint8_t weapon_slot[4];  /* 0x00511E5C */
    int32_t ally_matrix[17];  /* 0x00511E60, AllyFlag's [4][4] + the ALL row */
    uint8_t message_text[1024];  /* 0x00511EA4 */
    uint8_t message_bmp_name[32];  /* 0x005122A4 */
    uint8_t current_bitmap[4];  /* 0x005122C4 */
    uint8_t obj_ctx_obj_a[4];  /* 0x005122C8 */
    uint8_t obj_ctx_obj[4];  /* 0x005122CC */
    uint8_t obj_ctx_obj_prev[4];  /* 0x005122D0 */
    uint8_t weapon_fn_slot0[4];  /* 0x005122D4 */
    uint8_t weapon_fn_slot1[4];  /* 0x005122D8 */
    uint8_t weapon_fn_slot3[4];  /* 0x005122DC */
    uint8_t pointer_pick[4];  /* 0x005122E0 */
    uint8_t pointer_action[4];  /* 0x005122E4 */
    uint8_t pointer_f10[4];  /* 0x005122E8 */
    uint8_t pointer_f14[4];  /* 0x005122EC */
    uint8_t weapon_fn_slot2[4];  /* 0x005122F0 */
    uint8_t pointer_overlay[4];  /* 0x005122F4 */
    uint8_t second_deadline[4];  /* 0x005122F8 */
    uint8_t pause_flags[4];  /* 0x005122FC */
    uint8_t game_winner[4];  /* 0x00512300 */
    uint8_t win_enabled[4];  /* 0x00512304 */
    uint8_t selected_uids[4];  /* 0x00512308 */
    uint8_t selected_count[4];  /* 0x0051230C */
    uint8_t selected_items[4];  /* 0x00512310 */
    uint8_t game_seed[4];  /* 0x00512314 */
    uint8_t volume_at_zero[4];  /* 0x00512318 */
    uint8_t stream_volume[4];  /* 0x0051231C */
    uint8_t volume_voice[4];  /* 0x00512320 */
    uint8_t difficulty[4];  /* 0x00512324 */
    uint8_t movie_count[4];  /* 0x00512328 */
    uint8_t mission_retry[4];  /* 0x0051232C */
    uint8_t attempt_count[16];  /* 0x00512330 */
    uint8_t startup_colours[12];  /* 0x00512340 */
    uint8_t net_settle_count[4];  /* 0x0051234C */
    uint8_t perf_freq[8];  /* 0x00512350 */
    uint8_t cheat_invulnerable[4];  /* 0x00512358 */
    uint8_t game_dir[256];  /* 0x0051235C */
    uint8_t hwnd[4];  /* 0x0051245C */
    uint8_t full_redraw[4];  /* 0x00512460 */
    uint8_t cd_path[260];  /* 0x00512464 */
    uint8_t aim_x[2];  /* 0x00512568 */
    uint8_t aim_y[2];  /* 0x0051256A */
    uint8_t aim_z[4];  /* 0x0051256C */
    uint8_t perf_period[8];  /* 0x00512570 */
    uint8_t perf_start[8];  /* 0x00512578 */
    uint8_t hinstance[4];  /* 0x00512580 */
    uint8_t fast_machine[4];  /* 0x00512584 */
    uint8_t cd_present[12];  /* 0x00512588 */
    uint8_t cd_found_flag[4];  /* 0x00512594 */
    uint8_t fixed_step[4];  /* 0x00512598 */
    uint8_t opt_no_movies[4];  /* 0x0051259C */
    uint8_t zero_point[8];  /* 0x005125A0 */
    uint8_t zero_rect[16];  /* 0x005125A8 */
    uint8_t char_handler[8];  /* 0x005125B8 */
    uint8_t slow_machine[4];  /* 0x005125C0 */
    uint8_t opt_big_movies[4];  /* 0x005125C4 */
    uint8_t keys_buffer_a[256];  /* 0x005125C8 */
    uint8_t keys_buffer_b[256];  /* 0x005126C8 */
    uint8_t keys_now_ptr[4];  /* 0x005127C8 */
    uint8_t keys_prev_ptr[4];  /* 0x005127CC */
    uint32_t key_repeat_at[256];  /* 0x005127D0 */
    int32_t key_pressed[256];  /* 0x00512BD0 */
    uint8_t dinput[4];  /* 0x00512FD0 */
    uint8_t di_mouse[4];  /* 0x00512FD4 */
    uint8_t di_keyboard[4];  /* 0x00512FD8 */
    uint8_t di_device_3[4];  /* 0x00512FDC */
    uint8_t di_mouse_acquired[156];  /* 0x00512FE0 */
    uint8_t item_header_size[4];  /* 0x0051307C */
    uint8_t uid_remap_cap[4];  /* 0x00513080 */
    uint8_t uid_remap_count[4];  /* 0x00513084 */
    uint8_t uid_remap[4];  /* 0x00513088 */
    uint8_t iter_stamp[7428];  /* 0x0051308C */
    uint8_t map_block[64];  /* 0x00514D90 */
    uint8_t map_extent_x[4];  /* 0x00514DD0 */
    uint8_t map_extent_y[4];  /* 0x00514DD4 */
    uint8_t map_extent_shift[4];  /* 0x00514DD8 */
    uint8_t map_tiles_w[4];  /* 0x00514DDC */
    uint8_t map_tiles_h[4];  /* 0x00514DE0 */
    uint8_t map_row_shift[4];  /* 0x00514DE4 */
    uint8_t map_bounds[16];  /* 0x00514DE8 */
    uint8_t map_bounds_left[4];  /* 0x00514DF8 */
    uint8_t map_bounds_top[4];  /* 0x00514DFC */
    uint8_t map_bounds_right[4];  /* 0x00514E00 */
    uint8_t map_bounds_bottom[4];  /* 0x00514E04 */
    uint8_t view_target[4];  /* 0x00514E08 */
    uint8_t listener_pos[4];  /* 0x00514E0C */
    uint8_t listener_pos_prev[4];  /* 0x00514E10 */
    uint8_t view_origin_x[4];  /* 0x00514E14 */
    uint8_t view_origin_y[4];  /* 0x00514E18 */
    uint8_t view_far_x[4];  /* 0x00514E1C */
    uint8_t view_far_y[4];  /* 0x00514E20 */
    uint8_t view_rect_prev[16];  /* 0x00514E24 */
    uint8_t second_rect[16];  /* 0x00514E34 */
    uint8_t second_rect_prev[16];  /* 0x00514E44 */
    uint8_t view_clipped[16];  /* 0x00514E54 */
    uint8_t shake_time[4];  /* 0x00514E64 */
    uint8_t shake_phase_x[4];  /* 0x00514E68 */
    uint8_t shake_step_x[4];  /* 0x00514E6C */
    uint8_t shake_phase_y[4];  /* 0x00514E70 */
    uint8_t shake_step_y[4];  /* 0x00514E74 */
    uint8_t shake_amplitude[24];  /* 0x00514E78 */
    uint8_t map_surface[4];  /* 0x00514E90 */
    uint8_t map_cache_surface[8];  /* 0x00514E94 */
    uint8_t tile_shift_log[4];  /* 0x00514E9C */
    uint8_t view_tiles_w[4];  /* 0x00514EA0 */
    uint8_t view_tiles_h[4];  /* 0x00514EA4 */
    uint8_t visible_tiles[4];  /* 0x00514EA8 */
    uint8_t camera_y[12];  /* 0x00514EAC */
    uint8_t map_tiles[4];  /* 0x00514EB8 */
    uint8_t tile_attrs[4];  /* 0x00514EBC */
    uint8_t cell_weights[4];  /* 0x00514EC0 */
    uint8_t map_padbit_layer[4];  /* 0x00514EC4 */
    uint8_t map_pad_layer[4];  /* 0x00514EC8 */
    uint8_t region_of_cell[4];  /* 0x00514ECC */
    uint8_t tile_flags[4];  /* 0x00514ED0 */
    uint8_t tile_kind[4];  /* 0x00514ED4 */
    uint8_t *tile_reveal_grids[4];  /* 0x00514ED8 */
    uint8_t tile_cover[4];  /* 0x00514EE8 */
    uint8_t region_stride[4];  /* 0x00514EEC */
    uint8_t regions[4];  /* 0x00514EF0 */
    uint8_t region_next[4];  /* 0x00514EF4 */
    uint8_t region_stamp[4];  /* 0x00514EF8 */
    uint8_t region_cost[4];  /* 0x00514EFC */
    uint8_t obj_capacity[4];  /* 0x00514F00 */
    uint8_t obj_count[4];  /* 0x00514F04 */
    uint8_t iter_cursor[4];  /* 0x00514F08 */
    uint8_t obj_table[4];  /* 0x00514F0C */
    uint8_t obj_map_desc[16];  /* 0x00514F10 */
    uint8_t map_desc[80];  /* 0x00514F20 */
    uint8_t palette_cycle_stamp[16];  /* 0x00514F70 */
    uint8_t trig_sin[1536];  /* 0x00514F80 */
    uint8_t trig_atan_sin[516];  /* 0x00515580 */
    uint8_t trig_cos[1536];  /* 0x00515784 */
    uint8_t trig_atan_cos[516];  /* 0x00515D84 */
    uint8_t game_over_state[4];  /* 0x00515F88 */
    int32_t game_over_saved[3];  /* 0x00515F8C */
    uint8_t state_movie[8];  /* 0x00515F98 */
    void   *mp_panel_sprites_b[13];  /* 0x00515FA0 */
    uint8_t mp_panel_sprites_b_end[4];  /* 0x00515FD4 */
    uint8_t game_over_flags[4];  /* 0x00515FD8 */
    uint8_t game_setting_22c[4];  /* 0x00515FDC */
    int32_t army_points[4];  /* 0x00515FE0 */
    uint8_t score_limit[4];  /* 0x00515FF0 */
    uint8_t live_player_name[65];  /* 0x00515FF4 */
    uint8_t live_battle_name[67];  /* 0x00516035 */
    uint8_t host_mask_a[4];  /* 0x00516078 */
    uint8_t host_mask_b[20];  /* 0x0051607C */
    uint8_t host_value_3e8[4];  /* 0x00516090 */
    uint8_t saved_player_name[65];  /* 0x00516094 */
    uint8_t saved_battle_name[67];  /* 0x005160D5 */
    void   *mp_panel_sprites_a[5];  /* 0x00516118 */
    uint8_t menu_msg_list[4];  /* 0x0051612C */
    uint8_t session_object[8];  /* 0x00516130 */
    uint8_t record_list_cap[4];  /* 0x00516138 */
    uint8_t record_list_count[4];  /* 0x0051613C */
    uint8_t record_lists[4];  /* 0x00516140 */
    uint8_t aai_record_cap[4];  /* 0x00516144 */
    uint8_t key_table_count[4];  /* 0x00516148 */
    uint8_t aai_records[4];  /* 0x0051614C */
    uint8_t key_table[4];  /* 0x00516150 */
    uint8_t record_list_index[4];  /* 0x00516154 */
    uint8_t aai_key_980000[4];  /* 0x00516158 */
    uint8_t aai_key_980080[4];  /* 0x0051615C */
    uint8_t create_watched_kind[4];  /* 0x00516160 */
    uint8_t watched_type_id[4];  /* 0x00516164 */
    uint8_t aai_key_980100[4];  /* 0x00516168 */
    uint8_t kind7_count[4];  /* 0x0051616C */
    uint8_t def_obj_recs[4];  /* 0x00516170 */
    uint8_t def_obj_rec_count[4];  /* 0x00516174 */
    uint8_t def_obj_rec_cap[4];  /* 0x00516178 */
    uint8_t def_links[4];  /* 0x0051617C */
    uint8_t def_link_count[4];  /* 0x00516180 */
    uint8_t def_link_cap[4];  /* 0x00516184 */
    uint8_t obj_scripts[4];  /* 0x00516188 */
    uint8_t current_obj_script[4];  /* 0x0051618C */
    uint8_t obj_script_cap[8];  /* 0x00516190 */
    struct AM2_OS_Pad pads[512];  /* 0x00516198 */
    struct AM2_OS_PadNumber pad_numbers[256];  /* 0x0051F198 */
    uint8_t region_insert_prev[4];  /* 0x00523D98 */
    uint8_t region_neighbour[4];  /* 0x00523D9C */
    uint8_t tile_step8[32];  /* 0x00523DA0 */
    uint8_t region_goal[4];  /* 0x00523DC0 */
    uint8_t region_open_head[4];  /* 0x00523DC4 */
    uint8_t path_resume_head[4];  /* 0x00523DC8 */
    uint8_t region_resume_goalid[8];  /* 0x00523DCC */
    uint8_t region_walk[4];  /* 0x00523DD4 */
    uint8_t point_rule_army[4];  /* 0x00523DD8 */
    uint8_t point_rule[4];  /* 0x00523DDC */
    uint16_t tile_line_buf[50000];  /* 0x00523DE0 */
    uint8_t tile_ring8[72];  /* 0x0053C480 */
    uint8_t region_resume_node[100008];  /* 0x0053C4C8 */
    int32_t tile_ring4[4];  /* 0x00554B70 */
    uint8_t path_resume_goal[4];  /* 0x00554B80 */
    uint8_t tilemask_neighbours[84];  /* 0x00554B84 */
    AM2_PathNode path_nodes[65536];  /* 0x00554BD8 */
    int32_t tile_neighbours[20];  /* 0x00654BD8 */
    uint8_t region_current[4];  /* 0x00654C28 */
    uint8_t region_generation[4];  /* 0x00654C2C */
    uint8_t path_generation[4];  /* 0x00654C30 */
    uint8_t path_frame_ms[4];  /* 0x00654C34 */
    uint8_t path_frame_stamp[68];  /* 0x00654C38 */
    uint8_t placements[4];  /* 0x00654C7C */
    uint8_t placement_count[4];  /* 0x00654C80 */
    uint8_t placement_cap[12];  /* 0x00654C84 */
    uint8_t missile_anims[8];  /* 0x00654C90 */
    uint8_t roach_anims[8];  /* 0x00654C98 */
    uint8_t roach_mask_directions[8];  /* 0x00654CA0 */
    uint8_t roach_mask_count[4];  /* 0x00654CA8 */
    uint8_t roach_mask[5244];  /* 0x00654CAC */
    uint8_t roach_mark[512];  /* 0x00656128 */
    uint8_t roach_mark_stamp[8];  /* 0x00656328 */
    uint8_t scenario_unread[2];  /* 0x00656330 */
    uint8_t scenario_count[2];  /* 0x00656332 */
    uint8_t scenarios[4];  /* 0x00656334 */
    uint8_t level_table[4];  /* 0x00656338 */
    uint8_t level_table_count[4];  /* 0x0065633C */
    uint8_t level_table_cap[4];  /* 0x00656340 */
    uint8_t name_table_base[4];  /* 0x00656344 */
    uint8_t name_table_count[4];  /* 0x00656348 */
    uint8_t name_table_cap[4];  /* 0x0065634C */
    uint8_t svar_numgreen[4];  /* 0x00656350 */
    uint8_t script_word_buf[256];  /* 0x00656354 */
    uint8_t svar_blue[4];  /* 0x00656454 */
    uint8_t svar_me[8];  /* 0x00656458 */
    uint8_t script_name_cap[4];  /* 0x00656460 */
    uint8_t script_name_count[4];  /* 0x00656464 */
    uint8_t script_names[4];  /* 0x00656468 */
    uint8_t svar_grey[4];  /* 0x0065646C */
    uint8_t svar_systemspeed[4];  /* 0x00656470 */
    uint8_t svar_id15[4];  /* 0x00656474 */
    uint8_t script_context[12];  /* 0x00656478 */
    uint8_t svar_green[4];  /* 0x00656484 */
    uint8_t svar_greenscore[4];  /* 0x00656488 */
    uint8_t svar_tanscore[4];  /* 0x0065648C */
    uint8_t svar_bluescore[4];  /* 0x00656490 */
    uint8_t svar_greyscore[4];  /* 0x00656494 */
    uint8_t svar_tan[4];  /* 0x00656498 */
    uint8_t svar_difficulty[4];  /* 0x0065649C */
    uint32_t system_palette[256];  /* 0x006564A0 */
    uint8_t movie_current[12296];  /* 0x006568A0 */
    uint8_t movie_sound_ready[16];  /* 0x006598A8 */
    uint8_t sprite_file[4];  /* 0x006598B8 */
    uint8_t sprite_reg_cap[4];  /* 0x006598BC */
    uint8_t sprite_reg_count[4];  /* 0x006598C0 */
    uint8_t sprite_table[4];  /* 0x006598C4 */
    uint8_t sprite_reg_pairs[8];  /* 0x006598C8 */
    uint8_t glyph_size[4];  /* 0x006598D0 */
    uint8_t glyph_offsets[64];  /* 0x006598D4 */
    uint8_t glyph_offset_space[448];  /* 0x00659914 */
    uint8_t font_bases[1060];  /* 0x00659AD4 */
    uint8_t throttle_deadline[8];  /* 0x00659EF8 */
    uint8_t soldier_anims[72];  /* 0x00659F00 */
    uint8_t turn_repeat_ms[4];  /* 0x00659F48 */
    uint8_t def_trooper_recs[4];  /* 0x00659F4C */
    uint8_t def_trooper_count[4];  /* 0x00659F50 */
    uint8_t def_trooper_cap[4];  /* 0x00659F54 */
    uint8_t pending_confirm[256];  /* 0x00659F58 */
    uint8_t paint_object[4];  /* 0x0065A058 */
    uint8_t focused_edit[4];  /* 0x0065A05C */
    uint8_t movie_page[8];  /* 0x0065A060 */
    uint8_t slot_headings[64];  /* 0x0065A068 */
    uint8_t slot_positions[256];  /* 0x0065A0A8 */
    uint8_t slot_is_vehicle[256];  /* 0x0065A1A8 */
    uint8_t turret_anims[48];  /* 0x0065A2A8 */
    int32_t vehicle_mask_directions[6];  /* 0x0065A2D8 */
    uint8_t vehicle_mask_count[4];  /* 0x0065A2F0 */
    uint8_t vehicle_mask[31484];  /* 0x0065A2F4 */
    uint8_t vehicle_anims[48];  /* 0x00661DF0 */
    uint8_t obj_mark[512];  /* 0x00661E20 */
    uint8_t obj_mark_stamp[4];  /* 0x00662020 */
    uint8_t vehicle_defs[4];  /* 0x00662024 */
    uint8_t vehicle_def_count[4];  /* 0x00662028 */
    uint8_t vehicle_def_cap[4];  /* 0x0066202C */
    uint8_t missile_defs[44];  /* 0x00662030 */
    uint8_t spawn_kind_table[556];  /* 0x0066205C */
    uint8_t unused_662288[52];  /* 0x00662288 */
    uint8_t spawn_extra_6622bc[404];  /* 0x006622BC */
    uint8_t pick_reach_662450[156];  /* 0x00662450 */
    uint8_t pick_reach_6624ec[12];  /* 0x006624EC */
    uint8_t medic_heal_pct[612];  /* 0x006624F8 */
    uint8_t pick_reach_66275c[196];  /* 0x0066275C */
    uint8_t aim_life_half_a[24];  /* 0x00662820 */
    uint8_t aim_damage[28];  /* 0x00662838 */
    uint8_t aim_life_half_b[24];  /* 0x00662854 */
    uint8_t aim_spawn_arg[40];  /* 0x0066286C */
    uint8_t pick_reach_662894[12];  /* 0x00662894 */
    uint8_t repair_heal_pct[40];  /* 0x006628A0 */
    uint8_t pick_reach_6628c8[12];  /* 0x006628C8 */
    uint8_t spawn_extra_6628d4[40];  /* 0x006628D4 */
    uint8_t sweep_distance[12];  /* 0x006628FC */
    uint8_t sweep_damage[24];  /* 0x00662908 */
    uint8_t respawn_kinds[4];  /* 0x00662920 */
    uint8_t respawn_kind_count[4];  /* 0x00662924 */
    uint8_t def_missile_recs[4];  /* 0x00662928 */
    uint8_t def_missile_count[4];  /* 0x0066292C */
    uint8_t def_missile_cap[8];  /* 0x00662930 */
    uint8_t rowpool_a_count[4];  /* 0x00662938 */
    uint8_t rowpool_a_tail[4];  /* 0x0066293C */
    uint8_t rowpool_a_entries[12];  /* 0x00662940 */
    uint8_t spawn_kind_table_end[5988];  /* 0x0066294C */
    uint8_t seq_ctx_b[24];  /* 0x006640B0 */
    uint8_t rowpool_b_count[4];  /* 0x006640C8 */
    uint8_t rowpool_b_tail[4];  /* 0x006640CC */
    struct AM2_OS_RowPoolEntry rowpool_b_entries[100];  /* 0x006640D0 (B_CAP+B_BUDGET) */
    uint8_t seq_ctx_a[20];  /* 0x00664580 */
    uint8_t mp_mark_a[4];  /* 0x00664594 */
    uint8_t mp_mark_b[4];  /* 0x00664598 */
    uint8_t mp_mark_c[4];  /* 0x0066459C */
    uint8_t mp_team_sprites[4];  /* 0x006645A0 */
    uint8_t startup_colours_b[8];  /* 0x006645A4 */
    uint8_t crt_adjust_fdiv[4];  /* 0x006645AC */
    uint8_t crt_time_dst_cache[8];  /* 0x006645B0 */
    uint8_t crt_time_systime_cache[16];  /* 0x006645B8 */
    uint8_t crt_strtok_next[4];  /* 0x006645C8 */
    uint8_t crt_aenvptr[8];  /* 0x006645CC */
    uint8_t crt_error_mode[4];  /* 0x006645D4 */
    uint8_t crt_cftog_pflt[4];  /* 0x006645D8 */
    uint8_t crt_cftog_active[4];  /* 0x006645DC */
    uint8_t crt_cftog_magnitude[4];  /* 0x006645E0 */
    uint8_t crt_cftog_expansion[4];  /* 0x006645E4 */
    uint8_t crt_pnhheap[4];  /* 0x006645E8 */
    uint8_t crt_newmode[20];  /* 0x006645EC */
    uint8_t crt_errno[4];  /* 0x00664600 */
    uint8_t crt_doserrno[4];  /* 0x00664604 */
    uint8_t crt_umaskval[4];  /* 0x00664608 */
    uint8_t crt_osver[4];  /* 0x0066460C */
    uint8_t crt_winver[4];  /* 0x00664610 */
    uint8_t crt_winmajor[4];  /* 0x00664614 */
    uint8_t crt_winminor[4];  /* 0x00664618 */
    uint8_t crt_argc[4];  /* 0x0066461C */
    uint8_t crt_argv[8];  /* 0x00664620 */
    uint8_t crt_environ[8];  /* 0x00664628 */
    uint8_t crt_wenviron[8];  /* 0x00664630 */
    uint8_t crt_pgmptr[8];  /* 0x00664638 */
    uint8_t crt_exit_retcaller[4];  /* 0x00664640 */
    uint8_t crt_exit_started[4];  /* 0x00664644 */
    uint8_t crt_exit_done[44];  /* 0x00664648 */
    uint8_t crt_lc_handle_ctype[16];  /* 0x00664674 */
    uint8_t crt_lc_codepage[8];  /* 0x00664684 */
    uint8_t crt_cflush[16];  /* 0x0066468C */
    uint8_t crt_pgmname[260];  /* 0x0066469C */
    uint8_t crt_env_kind[4];  /* 0x006647A0 */
    uint8_t crt_msgbanner_hook[4];  /* 0x006647A4 */
    uint8_t crt_fltout_raw[32];  /* 0x006647A8 */
    uint8_t crt_strflt[16];  /* 0x006647C8 */
    uint8_t crt_commode[4];  /* 0x006647D8 */
    uint8_t crt_seh_old_filter[4];  /* 0x006647DC */
    uint8_t crt_tz_api_used[8];  /* 0x006647E0 */
    uint8_t crt_tz_info[172];  /* 0x006647E8 */
    uint8_t crt_last_tz[4];  /* 0x00664894 */
    uint8_t crt_tzset_done[8];  /* 0x00664898 */
    uint8_t crt_mbcp_from_system[4];  /* 0x006648A0 */
    uint8_t crt_msgbox_fn[4];  /* 0x006648A4 */
    uint8_t crt_getactivewindow_fn[4];  /* 0x006648A8 */
    uint8_t crt_getlastactivepopup_fn[4];  /* 0x006648AC */
    uint8_t crt_fmode[28];  /* 0x006648B0 */
    uint8_t crt_mbcodepage[4];  /* 0x006648CC */
    uint8_t crt_mbulinfo[12];  /* 0x006648D0 */
    uint8_t crt_ismbcodepage[4];  /* 0x006648DC */
    uint8_t crt_mbcasemap[256];  /* 0x006648E0 */
    uint8_t crt_mbctype[260];  /* 0x006649E0 */
    uint8_t crt_mblcid[4];  /* 0x00664AE4 */
    uint8_t crt_piob[4120];  /* 0x00664AE8 */
    uint8_t crt_nstream[32];  /* 0x00665B00 */
    uint8_t crt_pioinfo[256];  /* 0x00665B20 */
    uint8_t crt_nhandle[4];  /* 0x00665C20 */
    uint8_t crt_env_initialized[4];  /* 0x00665C24 */
    uint8_t crt_mbctable_init[4];  /* 0x00665C28 */
    uint8_t crt_onexitend[4];  /* 0x00665C2C */
    uint8_t crt_onexitbegin[4];  /* 0x00665C30 */
    uint8_t crt_sbh_size_header_list[4];  /* 0x00665C34 */
    uint8_t crt_sbh_ind_group_defer[4];  /* 0x00665C38 */
    uint8_t crt_sbh_pheader_scan[4];  /* 0x00665C3C */
    uint8_t crt_sbh_pheader_defer[4];  /* 0x00665C40 */
    uint8_t crt_sbh_cnt_header_list[4];  /* 0x00665C44 */
    uint8_t crt_sbh_pheader_list[4];  /* 0x00665C48 */
    uint8_t crt_crtheap[4];  /* 0x00665C4C */
    uint8_t crt_acmdln[944];  /* 0x00665C50 */
};

#ifdef __cplusplus
extern "C" struct AM2_OrigState am2_origstate;
#else
extern struct AM2_OrigState am2_origstate;
#endif

#endif /* AM2_ORIGSTATE_H */
