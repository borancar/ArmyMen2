/* The original's .bss working memory (0x0048E000..0x00666000) as one named
 * C struct placed there, REPLACING the anonymous .origbss the generator
 * used to emit. Each member is sized to land at its exact original VA, so
 * the reconstruction's ADDR_ macros still resolve and stored .data
 * pointers into this region stay valid. Zero at load = byte-identical to
 * the original's .bss. Members start as uint8_t[] and are upgraded to
 * typed records one at a time; the static_asserts below PIN every offset,
 * so a wrongly-sized typed member fails the compile rather than silently
 * shifting its neighbours. Generated once and committed.
 */
#include <stdint.h>
#include <stddef.h>

#ifdef AM2_STANDALONE

/* Record layouts for the typed members below. Kept local (rather than pulling
 * game headers into this flat file) and pinned by size: the offsetof asserts
 * after AM2_OrigState fail the build if any of these is not exactly its stride,
 * so a wrong field can't silently shift the struct. */
struct AM2_DepthNode {          /* ADDR_DEPTH_NODES, 12 bytes, {obj,prev,next} */
    void *obj, *prev, *next;
};
struct AM2_Timer {              /* ADDR_TIMER_TABLE, 16 bytes */
    uint32_t start;             /* mission ms of the next fire */
    uint32_t period;            /* ms between fires */
    int32_t  count;             /* fires remaining */
    int32_t  id;                /* 0 = free slot */
};
struct AM2_PadNumber {          /* ADDR_PAD_NUMBERS, 76 bytes */
    int16_t count;              /* +0x00 */
    int16_t pads[32];           /* +0x02, indices into ADDR_PADS */
    int16_t cx, cy;             /* +0x42, +0x44, sixteenths of a cell */
    uint8_t rest[76 - 0x46];
};
struct AM2_Pad {                /* ADDR_PADS, 72 bytes (script.h's AM2_Pad) */
    int32_t id, number, name, compared, specific, trigger, compare, threshold;
    int32_t delay0, delay1;     /* +0x00..+0x24, written by the parser */
    uint8_t rest[0x38 - 0x28];  /* +0x28 the event key, written via event.cpp */
    int32_t period;             /* +0x38, repeat period ms */
    int32_t dueAt;              /* +0x3C, game-clock ms */
    uint8_t rest40[72 - 0x40];
};
struct AM2_PlayerRecord {       /* ADDR_PLAYER_RECORDS, 0x7E0 = 2016 bytes, six of them */
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
struct AM2_PacketRecord {       /* ADDR_PACKET_RECORDS, 0x28 = 40 bytes, 400 of them */
    uint8_t  head[0x10];        /* +0x00 (len@4, seq@8, checksum@0xC on the wire view) */
    uint32_t size;              /* +0x10, PACKET_REC_OFF_SIZE */
    uint32_t key;               /* +0x14, PACKET_REC_OFF_KEY */
    uint32_t flags;             /* +0x18, PACKET_REC_OFF_FLAGS */
    uint8_t  mid[0x20 - 0x1C];  /* +0x1C */
    void    *data;              /* +0x20, PACKET_REC_OFF_DATA -> a 0x400 buffer */
    uint8_t  tail[0x28 - 0x24]; /* +0x24 */
};
struct AM2_RowPoolEntry {       /* AM2_ROWPOOL_ENTRY_BYTES = 12 */
    void    *row;               /* +0x00, ROWPOOL_OFF_ROW -> a 0x60 row block */
    uint16_t id;                /* +0x04, ROWPOOL_OFF_ID */
    uint16_t prev;              /* +0x06, ROWPOOL_OFF_PREV (slot index) */
    uint16_t next;              /* +0x08, ROWPOOL_OFF_NEXT (slot index) */
    uint16_t _pad;              /* +0x0A */
};
struct AM2_SoundSlot {          /* SOUND_SLOT_STRIDE = 16, 56 fixed slots */
    void    *buffer;            /* +0x00, SOUND_SLOT_OFF_BUFFER (a DirectSound buffer;
                                 *  void* here -- this flat file names no COM type) */
    uint32_t bytes;             /* +0x04, SOUND_SLOT_OFF_BYTES */
    int32_t  volume;            /* +0x08, SOUND_SLOT_OFF_VOLUME */
    uint32_t started;           /* +0x0C, SOUND_SLOT_OFF_STARTED */
};
struct AM2_SightDir {           /* AM2_SIGHT_DIR_STRIDE = 16, record[64] by bearing */
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
    struct AM2_PlayerRecord player_records[6];  /* 0x004F1980 */
    uint8_t default_player_evt[8];  /* 0x004F48C0 */
    uint8_t msg_list_b[16];  /* 0x004F48C8 */
    uint8_t packet_thread[4];  /* 0x004F48D8 */
    uint8_t player_slot_mask[4];  /* 0x004F48DC */
    uint8_t comm_no_buffers_latch[16];  /* 0x004F48E0 */
    uint8_t last_msg_value[4];  /* 0x004F48F0 */
    uint8_t last_msg_checksum[4];  /* 0x004F48F4 */
    struct AM2_PacketRecord packet_records[400];  /* 0x004F48F8 */
    uint8_t exit_game_flag[4];  /* 0x004F8778 */
    uint8_t packet_state[4];  /* 0x004F877C */
    uint8_t msg_list_delayed[16];  /* 0x004F8780 */
    uint8_t recv_scratch[1024];  /* 0x004F8790 */
    uint8_t packet_thread_id[8];  /* 0x004F8B90 */
    uint8_t msg_wanted_flags[16];  /* 0x004F8B98 */
    uint8_t send_roll[1040];  /* 0x004F8BA8 */
    struct AM2_SightDir sight_block_by_dir[64];  /* 0x004F8FB8 */
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
    struct AM2_SoundSlot sound_slots[56];  /* 0x004FA040 .. 0x004FA3C0 */
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
    uint8_t msg_game_ready[40];  /* 0x004FAA28 */
    uint8_t msg_game_pause[24];  /* 0x004FAA50 */
    uint8_t army_packet[4];  /* 0x004FAA68 */
    uint8_t army_packet_len[4];  /* 0x004FAA6C */
    uint8_t army_packet_seq[1016];  /* 0x004FAA70 */
    uint8_t resend_buf[2312];  /* 0x004FAE68 */
    uint8_t msg_map[16];  /* 0x004FB770 */
    uint8_t resend_scratch[3112];  /* 0x004FB780 */
    uint8_t msg_end_setup[16];  /* 0x004FC3A8 */
    uint8_t player_msg[568];  /* 0x004FC3B8 */
    uint8_t msg_game_start[400];  /* 0x004FC5F0 */
    uint8_t game_seed_sent[280];  /* 0x004FC780 */
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
    uint8_t colour_white[2060];  /* 0x004FD768 */
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
    uint8_t colour_olive[258];  /* 0x004FE1AE */
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
    struct AM2_DepthNode depth_nodes[500];  /* 0x00507350 */
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
    struct AM2_Timer timer_table[1000];  /* 0x0050C370 */
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
    uint8_t ally_matrix[68];  /* 0x00511E60 */
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
    struct AM2_Pad pads[512];  /* 0x00516198 */
    struct AM2_PadNumber pad_numbers[256];  /* 0x0051F198 */
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
    uint8_t path_nodes[65536][16];  /* 0x00554BD8, [0x10000] nodes of 16 bytes */
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
    struct AM2_RowPoolEntry rowpool_b_entries[100];  /* 0x006640D0 (B_CAP+B_BUDGET) */
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

/* Every named member pinned to its VA -- a mis-sized member drifts the
 * next offsetof and fails HERE, at compile time. */
static_assert(offsetof(struct AM2_OrigState, packet_buffers_end) == 0x004F1978u - 0x0048E000u, "packet_buffers_end");
static_assert(offsetof(struct AM2_OrigState, player_records) == 0x004F1980u - 0x0048E000u, "player_records");
static_assert(offsetof(struct AM2_OrigState, default_player_evt) == 0x004F48C0u - 0x0048E000u, "default_player_evt");
static_assert(offsetof(struct AM2_OrigState, msg_list_b) == 0x004F48C8u - 0x0048E000u, "msg_list_b");
static_assert(offsetof(struct AM2_OrigState, packet_thread) == 0x004F48D8u - 0x0048E000u, "packet_thread");
static_assert(offsetof(struct AM2_OrigState, player_slot_mask) == 0x004F48DCu - 0x0048E000u, "player_slot_mask");
static_assert(offsetof(struct AM2_OrigState, comm_no_buffers_latch) == 0x004F48E0u - 0x0048E000u, "comm_no_buffers_latch");
static_assert(offsetof(struct AM2_OrigState, last_msg_value) == 0x004F48F0u - 0x0048E000u, "last_msg_value");
static_assert(offsetof(struct AM2_OrigState, last_msg_checksum) == 0x004F48F4u - 0x0048E000u, "last_msg_checksum");
static_assert(offsetof(struct AM2_OrigState, packet_records) == 0x004F48F8u - 0x0048E000u, "packet_records");
static_assert(offsetof(struct AM2_OrigState, exit_game_flag) == 0x004F8778u - 0x0048E000u, "exit_game_flag");
static_assert(offsetof(struct AM2_OrigState, packet_state) == 0x004F877Cu - 0x0048E000u, "packet_state");
static_assert(offsetof(struct AM2_OrigState, msg_list_delayed) == 0x004F8780u - 0x0048E000u, "msg_list_delayed");
static_assert(offsetof(struct AM2_OrigState, recv_scratch) == 0x004F8790u - 0x0048E000u, "recv_scratch");
static_assert(offsetof(struct AM2_OrigState, packet_thread_id) == 0x004F8B90u - 0x0048E000u, "packet_thread_id");
static_assert(offsetof(struct AM2_OrigState, msg_wanted_flags) == 0x004F8B98u - 0x0048E000u, "msg_wanted_flags");
static_assert(offsetof(struct AM2_OrigState, send_roll) == 0x004F8BA8u - 0x0048E000u, "send_roll");
static_assert(offsetof(struct AM2_OrigState, sight_block_by_dir) == 0x004F8FB8u - 0x0048E000u, "sight_block_by_dir");
static_assert(offsetof(struct AM2_OrigState, perframe_count_a) == 0x004F93B8u - 0x0048E000u, "perframe_count_a");
static_assert(offsetof(struct AM2_OrigState, perframe_count_b) == 0x004F93BCu - 0x0048E000u, "perframe_count_b");
static_assert(offsetof(struct AM2_OrigState, sight_generation) == 0x004F93C0u - 0x0048E000u, "sight_generation");
static_assert(offsetof(struct AM2_OrigState, air_leg1_dy) == 0x004F93C8u - 0x0048E000u, "air_leg1_dy");
static_assert(offsetof(struct AM2_OrigState, air_leg2_divisor) == 0x004F93CCu - 0x0048E000u, "air_leg2_divisor");
static_assert(offsetof(struct AM2_OrigState, air_leg2_ms_span) == 0x004F93D0u - 0x0048E000u, "air_leg2_ms_span");
static_assert(offsetof(struct AM2_OrigState, air_leg3_dx) == 0x004F93D4u - 0x0048E000u, "air_leg3_dx");
static_assert(offsetof(struct AM2_OrigState, air_sprites_2) == 0x004F93D8u - 0x0048E000u, "air_sprites_2");
static_assert(offsetof(struct AM2_OrigState, air_sprites_3) == 0x004F93DCu - 0x0048E000u, "air_sprites_3");
static_assert(offsetof(struct AM2_OrigState, air_sprites_6) == 0x004F942Cu - 0x0048E000u, "air_sprites_6");
static_assert(offsetof(struct AM2_OrigState, air_sprites_edge) == 0x004F9458u - 0x0048E000u, "air_sprites_edge");
static_assert(offsetof(struct AM2_OrigState, air_save_block) == 0x004F945Cu - 0x0048E000u, "air_save_block");
static_assert(offsetof(struct AM2_OrigState, air_leg2_dy) == 0x004F96A4u - 0x0048E000u, "air_leg2_dy");
static_assert(offsetof(struct AM2_OrigState, air_leg3_dy) == 0x004F96A8u - 0x0048E000u, "air_leg3_dy");
static_assert(offsetof(struct AM2_OrigState, air_leg1_x0) == 0x004F96ACu - 0x0048E000u, "air_leg1_x0");
static_assert(offsetof(struct AM2_OrigState, air_leg1_dx) == 0x004F96B0u - 0x0048E000u, "air_leg1_dx");
static_assert(offsetof(struct AM2_OrigState, air_leg3_x1) == 0x004F96B4u - 0x0048E000u, "air_leg3_x1");
static_assert(offsetof(struct AM2_OrigState, dir_scratch) == 0x004F96B8u - 0x0048E000u, "dir_scratch");
static_assert(offsetof(struct AM2_OrigState, sprite_list_cap) == 0x004F96C0u - 0x0048E000u, "sprite_list_cap");
static_assert(offsetof(struct AM2_OrigState, sprite_list_count) == 0x004F96C4u - 0x0048E000u, "sprite_list_count");
static_assert(offsetof(struct AM2_OrigState, sprite_list) == 0x004F96C8u - 0x0048E000u, "sprite_list");
static_assert(offsetof(struct AM2_OrigState, army_ramp_tables) == 0x004F96CCu - 0x0048E000u, "army_ramp_tables");
static_assert(offsetof(struct AM2_OrigState, obj_table_records) == 0x004F9ACCu - 0x0048E000u, "obj_table_records");
static_assert(offsetof(struct AM2_OrigState, chat_colour_table) == 0x004F9AD0u - 0x0048E000u, "chat_colour_table");
static_assert(offsetof(struct AM2_OrigState, army_obj_lists) == 0x004F9ECCu - 0x0048E000u, "army_obj_lists");
static_assert(offsetof(struct AM2_OrigState, type2_action_list) == 0x004F9ED0u - 0x0048E000u, "type2_action_list");
static_assert(offsetof(struct AM2_OrigState, row_lut_doubles) == 0x004F9EDCu - 0x0048E000u, "row_lut_doubles");
static_assert(offsetof(struct AM2_OrigState, default_owner) == 0x004F9FDCu - 0x0048E000u, "default_owner");
static_assert(offsetof(struct AM2_OrigState, wincpuid_fn) == 0x004F9FE0u - 0x0048E000u, "wincpuid_fn");
static_assert(offsetof(struct AM2_OrigState, last_message) == 0x004F9FE4u - 0x0048E000u, "last_message");
static_assert(offsetof(struct AM2_OrigState, cpunormspeed_fn) == 0x004F9FE8u - 0x0048E000u, "cpunormspeed_fn");
static_assert(offsetof(struct AM2_OrigState, opt_map_name) == 0x004F9FECu - 0x0048E000u, "opt_map_name");
static_assert(offsetof(struct AM2_OrigState, app_active) == 0x004FA02Cu - 0x0048E000u, "app_active");
static_assert(offsetof(struct AM2_OrigState, present_enabled) == 0x004FA030u - 0x0048E000u, "present_enabled");
static_assert(offsetof(struct AM2_OrigState, app_mutex) == 0x004FA034u - 0x0048E000u, "app_mutex");
static_assert(offsetof(struct AM2_OrigState, opt_no_intro) == 0x004FA038u - 0x0048E000u, "opt_no_intro");
static_assert(offsetof(struct AM2_OrigState, sound_slots) == 0x004FA040u - 0x0048E000u, "sound_slots");
static_assert(offsetof(struct AM2_OrigState, sound_slots_end) == 0x004FA3C0u - 0x0048E000u, "sound_slots_end");
static_assert(offsetof(struct AM2_OrigState, sound_dynamic_last) == 0x004FA400u - 0x0048E000u, "sound_dynamic_last");
static_assert(offsetof(struct AM2_OrigState, audio_buffer) == 0x004FA404u - 0x0048E000u, "audio_buffer");
static_assert(offsetof(struct AM2_OrigState, audio_timer_id) == 0x004FA408u - 0x0048E000u, "audio_timer_id");
static_assert(offsetof(struct AM2_OrigState, audio_waveformat) == 0x004FA410u - 0x0048E000u, "audio_waveformat");
static_assert(offsetof(struct AM2_OrigState, audio_hmmio) == 0x004FA414u - 0x0048E000u, "audio_hmmio");
static_assert(offsetof(struct AM2_OrigState, audio_data_chunk) == 0x004FA418u - 0x0048E000u, "audio_data_chunk");
static_assert(offsetof(struct AM2_OrigState, audio_riff_chunk) == 0x004FA42Cu - 0x0048E000u, "audio_riff_chunk");
static_assert(offsetof(struct AM2_OrigState, audio_buffer_2) == 0x004FA440u - 0x0048E000u, "audio_buffer_2");
static_assert(offsetof(struct AM2_OrigState, audio_buffer_size) == 0x004FA444u - 0x0048E000u, "audio_buffer_size");
static_assert(offsetof(struct AM2_OrigState, audio_period) == 0x004FA448u - 0x0048E000u, "audio_period");
static_assert(offsetof(struct AM2_OrigState, audio_cursor_b) == 0x004FA44Cu - 0x0048E000u, "audio_cursor_b");
static_assert(offsetof(struct AM2_OrigState, audio_cursor_a) == 0x004FA450u - 0x0048E000u, "audio_cursor_a");
static_assert(offsetof(struct AM2_OrigState, audio_valid_bytes) == 0x004FA454u - 0x0048E000u, "audio_valid_bytes");
static_assert(offsetof(struct AM2_OrigState, audio_read_failed) == 0x004FA458u - 0x0048E000u, "audio_read_failed");
static_assert(offsetof(struct AM2_OrigState, audio_looping) == 0x004FA45Cu - 0x0048E000u, "audio_looping");
static_assert(offsetof(struct AM2_OrigState, audio_at_end) == 0x004FA460u - 0x0048E000u, "audio_at_end");
static_assert(offsetof(struct AM2_OrigState, audio_timer_run) == 0x004FA464u - 0x0048E000u, "audio_timer_run");
static_assert(offsetof(struct AM2_OrigState, audio_enabled) == 0x004FA468u - 0x0048E000u, "audio_enabled");
static_assert(offsetof(struct AM2_OrigState, dsound) == 0x004FA46Cu - 0x0048E000u, "dsound");
static_assert(offsetof(struct AM2_OrigState, ds_primary) == 0x004FA470u - 0x0048E000u, "ds_primary");
static_assert(offsetof(struct AM2_OrigState, ds_listener) == 0x004FA474u - 0x0048E000u, "ds_listener");
static_assert(offsetof(struct AM2_OrigState, audio_in_callback) == 0x004FA478u - 0x0048E000u, "audio_in_callback");
static_assert(offsetof(struct AM2_OrigState, comm_global) == 0x004FA480u - 0x0048E000u, "comm_global");
static_assert(offsetof(struct AM2_OrigState, connection_list) == 0x004FA900u - 0x0048E000u, "connection_list");
static_assert(offsetof(struct AM2_OrigState, our_slot) == 0x004FA904u - 0x0048E000u, "our_slot");
static_assert(offsetof(struct AM2_OrigState, session_list) == 0x004FA908u - 0x0048E000u, "session_list");
static_assert(offsetof(struct AM2_OrigState, msg_chat) == 0x004FA910u - 0x0048E000u, "msg_chat");
static_assert(offsetof(struct AM2_OrigState, msg_ready_to_load) == 0x004FAA18u - 0x0048E000u, "msg_ready_to_load");
static_assert(offsetof(struct AM2_OrigState, msg_game_ready) == 0x004FAA28u - 0x0048E000u, "msg_game_ready");
static_assert(offsetof(struct AM2_OrigState, msg_game_pause) == 0x004FAA50u - 0x0048E000u, "msg_game_pause");
static_assert(offsetof(struct AM2_OrigState, army_packet) == 0x004FAA68u - 0x0048E000u, "army_packet");
static_assert(offsetof(struct AM2_OrigState, army_packet_len) == 0x004FAA6Cu - 0x0048E000u, "army_packet_len");
static_assert(offsetof(struct AM2_OrigState, army_packet_seq) == 0x004FAA70u - 0x0048E000u, "army_packet_seq");
static_assert(offsetof(struct AM2_OrigState, resend_buf) == 0x004FAE68u - 0x0048E000u, "resend_buf");
static_assert(offsetof(struct AM2_OrigState, msg_map) == 0x004FB770u - 0x0048E000u, "msg_map");
static_assert(offsetof(struct AM2_OrigState, resend_scratch) == 0x004FB780u - 0x0048E000u, "resend_scratch");
static_assert(offsetof(struct AM2_OrigState, msg_end_setup) == 0x004FC3A8u - 0x0048E000u, "msg_end_setup");
static_assert(offsetof(struct AM2_OrigState, player_msg) == 0x004FC3B8u - 0x0048E000u, "player_msg");
static_assert(offsetof(struct AM2_OrigState, msg_game_start) == 0x004FC5F0u - 0x0048E000u, "msg_game_start");
static_assert(offsetof(struct AM2_OrigState, game_seed_sent) == 0x004FC780u - 0x0048E000u, "game_seed_sent");
static_assert(offsetof(struct AM2_OrigState, msg_color) == 0x004FC898u - 0x0048E000u, "msg_color");
static_assert(offsetof(struct AM2_OrigState, msg_team) == 0x004FC8A8u - 0x0048E000u, "msg_team");
static_assert(offsetof(struct AM2_OrigState, data_checksum) == 0x004FC8B4u - 0x0048E000u, "data_checksum");
static_assert(offsetof(struct AM2_OrigState, timeout_logged_at) == 0x004FC8B8u - 0x0048E000u, "timeout_logged_at");
static_assert(offsetof(struct AM2_OrigState, change_limit_floor) == 0x004FC8BCu - 0x0048E000u, "change_limit_floor");
static_assert(offsetof(struct AM2_OrigState, bandwidth_last_seq) == 0x004FC8C0u - 0x0048E000u, "bandwidth_last_seq");
static_assert(offsetof(struct AM2_OrigState, aim_sprites_a) == 0x004FC8C8u - 0x0048E000u, "aim_sprites_a");
static_assert(offsetof(struct AM2_OrigState, aim_live_a) == 0x004FC8E0u - 0x0048E000u, "aim_live_a");
static_assert(offsetof(struct AM2_OrigState, aim_point_a) == 0x004FC8F0u - 0x0048E000u, "aim_point_a");
static_assert(offsetof(struct AM2_OrigState, aim_stamp_a) == 0x004FC900u - 0x0048E000u, "aim_stamp_a");
static_assert(offsetof(struct AM2_OrigState, aim_deadline_a) == 0x004FC910u - 0x0048E000u, "aim_deadline_a");
static_assert(offsetof(struct AM2_OrigState, aim_sprites_b) == 0x004FC920u - 0x0048E000u, "aim_sprites_b");
static_assert(offsetof(struct AM2_OrigState, aim_live_b) == 0x004FC944u - 0x0048E000u, "aim_live_b");
static_assert(offsetof(struct AM2_OrigState, aim_point_b) == 0x004FC954u - 0x0048E000u, "aim_point_b");
static_assert(offsetof(struct AM2_OrigState, aim_frame_b) == 0x004FC964u - 0x0048E000u, "aim_frame_b");
static_assert(offsetof(struct AM2_OrigState, aim_stamp_b) == 0x004FC974u - 0x0048E000u, "aim_stamp_b");
static_assert(offsetof(struct AM2_OrigState, aim_deadline_b) == 0x004FC984u - 0x0048E000u, "aim_deadline_b");
static_assert(offsetof(struct AM2_OrigState, palette_glyphs) == 0x004FC998u - 0x0048E000u, "palette_glyphs");
static_assert(offsetof(struct AM2_OrigState, menu_saved_rect) == 0x004FCA98u - 0x0048E000u, "menu_saved_rect");
static_assert(offsetof(struct AM2_OrigState, menu_row) == 0x004FCAA8u - 0x0048E000u, "menu_row");
static_assert(offsetof(struct AM2_OrigState, menu_sprites) == 0x004FCAACu - 0x0048E000u, "menu_sprites");
static_assert(offsetof(struct AM2_OrigState, menu_sprites_end) == 0x004FCDA4u - 0x0048E000u, "menu_sprites_end");
static_assert(offsetof(struct AM2_OrigState, menu_overlay_a) == 0x004FCDA8u - 0x0048E000u, "menu_overlay_a");
static_assert(offsetof(struct AM2_OrigState, menu_overlay_b) == 0x004FCDACu - 0x0048E000u, "menu_overlay_b");
static_assert(offsetof(struct AM2_OrigState, menu_ink) == 0x004FCDB0u - 0x0048E000u, "menu_ink");
static_assert(offsetof(struct AM2_OrigState, menu_overlay_a_ink) == 0x004FCDB4u - 0x0048E000u, "menu_overlay_a_ink");
static_assert(offsetof(struct AM2_OrigState, menu_overlay_b_ink) == 0x004FCDB8u - 0x0048E000u, "menu_overlay_b_ink");
static_assert(offsetof(struct AM2_OrigState, menu_cursor_dx) == 0x004FCDBCu - 0x0048E000u, "menu_cursor_dx");
static_assert(offsetof(struct AM2_OrigState, menu_overlay_a_dx) == 0x004FCDC0u - 0x0048E000u, "menu_overlay_a_dx");
static_assert(offsetof(struct AM2_OrigState, menu_overlay_b_dx) == 0x004FCDC4u - 0x0048E000u, "menu_overlay_b_dx");
static_assert(offsetof(struct AM2_OrigState, menu_cursor_prev) == 0x004FCDC8u - 0x0048E000u, "menu_cursor_prev");
static_assert(offsetof(struct AM2_OrigState, menu_cursor_rect) == 0x004FCDD8u - 0x0048E000u, "menu_cursor_rect");
static_assert(offsetof(struct AM2_OrigState, menu_anim_next) == 0x004FCDE8u - 0x0048E000u, "menu_anim_next");
static_assert(offsetof(struct AM2_OrigState, menu_anim_frame) == 0x004FCDECu - 0x0048E000u, "menu_anim_frame");
static_assert(offsetof(struct AM2_OrigState, menu_row_stamp) == 0x004FCDF0u - 0x0048E000u, "menu_row_stamp");
static_assert(offsetof(struct AM2_OrigState, menu_surface) == 0x004FCDF4u - 0x0048E000u, "menu_surface");
static_assert(offsetof(struct AM2_OrigState, flame_record) == 0x004FCDF8u - 0x0048E000u, "flame_record");
static_assert(offsetof(struct AM2_OrigState, menu_enabled) == 0x004FCEF8u - 0x0048E000u, "menu_enabled");
static_assert(offsetof(struct AM2_OrigState, menu_saved_valid) == 0x004FCEFCu - 0x0048E000u, "menu_saved_valid");
static_assert(offsetof(struct AM2_OrigState, hud_widget_a) == 0x004FCF00u - 0x0048E000u, "hud_widget_a");
static_assert(offsetof(struct AM2_OrigState, hud_widget_table) == 0x004FCF04u - 0x0048E000u, "hud_widget_table");
static_assert(offsetof(struct AM2_OrigState, hud_widget_c) == 0x004FCF4Cu - 0x0048E000u, "hud_widget_c");
static_assert(offsetof(struct AM2_OrigState, hud_index) == 0x004FCF50u - 0x0048E000u, "hud_index");
static_assert(offsetof(struct AM2_OrigState, hud_widget_b) == 0x004FCF54u - 0x0048E000u, "hud_widget_b");
static_assert(offsetof(struct AM2_OrigState, view_rect_on) == 0x004FCF58u - 0x0048E000u, "view_rect_on");
static_assert(offsetof(struct AM2_OrigState, radar_colours) == 0x004FCF5Cu - 0x0048E000u, "radar_colours");
static_assert(offsetof(struct AM2_OrigState, drag_anchor) == 0x004FCF68u - 0x0048E000u, "drag_anchor");
static_assert(offsetof(struct AM2_OrigState, view_rect) == 0x004FCF70u - 0x0048E000u, "view_rect");
static_assert(offsetof(struct AM2_OrigState, pointer_mode) == 0x004FCF80u - 0x0048E000u, "pointer_mode");
static_assert(offsetof(struct AM2_OrigState, hud_dirty) == 0x004FCF84u - 0x0048E000u, "hud_dirty");
static_assert(offsetof(struct AM2_OrigState, place_flag_4fcf88) == 0x004FCF88u - 0x0048E000u, "place_flag_4fcf88");
static_assert(offsetof(struct AM2_OrigState, place_facing) == 0x004FCF8Cu - 0x0048E000u, "place_facing");
static_assert(offsetof(struct AM2_OrigState, number_key_slot) == 0x004FCF90u - 0x0048E000u, "number_key_slot");
static_assert(offsetof(struct AM2_OrigState, cheat_enabled) == 0x004FCF94u - 0x0048E000u, "cheat_enabled");
static_assert(offsetof(struct AM2_OrigState, cheat_level_select) == 0x004FCF98u - 0x0048E000u, "cheat_level_select");
static_assert(offsetof(struct AM2_OrigState, our_points) == 0x004FCF9Cu - 0x0048E000u, "our_points");
static_assert(offsetof(struct AM2_OrigState, flame_on) == 0x004FCFA0u - 0x0048E000u, "flame_on");
static_assert(offsetof(struct AM2_OrigState, flame_next_ms) == 0x004FCFA4u - 0x0048E000u, "flame_next_ms");
static_assert(offsetof(struct AM2_OrigState, view_colour_copy) == 0x004FCFA8u - 0x0048E000u, "view_colour_copy");
static_assert(offsetof(struct AM2_OrigState, opt_rob) == 0x004FD73Cu - 0x0048E000u, "opt_rob");
static_assert(offsetof(struct AM2_OrigState, opt_dan) == 0x004FD740u - 0x0048E000u, "opt_dan");
static_assert(offsetof(struct AM2_OrigState, opt_peter) == 0x004FD744u - 0x0048E000u, "opt_peter");
static_assert(offsetof(struct AM2_OrigState, opt_4fd748) == 0x004FD748u - 0x0048E000u, "opt_4fd748");
static_assert(offsetof(struct AM2_OrigState, colour_dark_blue) == 0x004FD760u - 0x0048E000u, "colour_dark_blue");
static_assert(offsetof(struct AM2_OrigState, remap_identity) == 0x004FD764u - 0x0048E000u, "remap_identity");
static_assert(offsetof(struct AM2_OrigState, colour_white) == 0x004FD768u - 0x0048E000u, "colour_white");
static_assert(offsetof(struct AM2_OrigState, colour_dark_green) == 0x004FDF74u - 0x0048E000u, "colour_dark_green");
static_assert(offsetof(struct AM2_OrigState, colour_cream) == 0x004FDF75u - 0x0048E000u, "colour_cream");
static_assert(offsetof(struct AM2_OrigState, directdraw) == 0x004FDF78u - 0x0048E000u, "directdraw");
static_assert(offsetof(struct AM2_OrigState, colour_blue) == 0x004FDF7Cu - 0x0048E000u, "colour_blue");
static_assert(offsetof(struct AM2_OrigState, surface_locked) == 0x004FDF80u - 0x0048E000u, "surface_locked");
static_assert(offsetof(struct AM2_OrigState, default_palette) == 0x004FE084u - 0x0048E000u, "default_palette");
static_assert(offsetof(struct AM2_OrigState, colour_steel_blue) == 0x004FE088u - 0x0048E000u, "colour_steel_blue");
static_assert(offsetof(struct AM2_OrigState, view_rect_colour) == 0x004FE089u - 0x0048E000u, "view_rect_colour");
static_assert(offsetof(struct AM2_OrigState, back_buffer) == 0x004FE08Cu - 0x0048E000u, "back_buffer");
static_assert(offsetof(struct AM2_OrigState, colour_stale) == 0x004FE090u - 0x0048E000u, "colour_stale");
static_assert(offsetof(struct AM2_OrigState, colour_light_green) == 0x004FE091u - 0x0048E000u, "colour_light_green");
static_assert(offsetof(struct AM2_OrigState, colour_lag_mid) == 0x004FE092u - 0x0048E000u, "colour_lag_mid");
static_assert(offsetof(struct AM2_OrigState, colour_light_grey) == 0x004FE093u - 0x0048E000u, "colour_light_grey");
static_assert(offsetof(struct AM2_OrigState, colour_black) == 0x004FE094u - 0x0048E000u, "colour_black");
static_assert(offsetof(struct AM2_OrigState, directdraw2) == 0x004FE098u - 0x0048E000u, "directdraw2");
static_assert(offsetof(struct AM2_OrigState, remap_bright_buf) == 0x004FE09Cu - 0x0048E000u, "remap_bright_buf");
static_assert(offsetof(struct AM2_OrigState, overlay_palette) == 0x004FE1A4u - 0x0048E000u, "overlay_palette");
static_assert(offsetof(struct AM2_OrigState, framebuffer) == 0x004FE1A8u - 0x0048E000u, "framebuffer");
static_assert(offsetof(struct AM2_OrigState, colour_light_blue) == 0x004FE1ACu - 0x0048E000u, "colour_light_blue");
static_assert(offsetof(struct AM2_OrigState, colour_white_b) == 0x004FE1ADu - 0x0048E000u, "colour_white_b");
static_assert(offsetof(struct AM2_OrigState, colour_olive) == 0x004FE1AEu - 0x0048E000u, "colour_olive");
static_assert(offsetof(struct AM2_OrigState, remap_shades) == 0x004FE2B0u - 0x0048E000u, "remap_shades");
static_assert(offsetof(struct AM2_OrigState, variation_blocks) == 0x004FE2C0u - 0x0048E000u, "variation_blocks");
static_assert(offsetof(struct AM2_OrigState, variation_end) == 0x005022C0u - 0x0048E000u, "variation_end");
static_assert(offsetof(struct AM2_OrigState, palette_copy) == 0x005022C8u - 0x0048E000u, "palette_copy");
static_assert(offsetof(struct AM2_OrigState, list_ink_hot_sel) == 0x00502ACCu - 0x0048E000u, "list_ink_hot_sel");
static_assert(offsetof(struct AM2_OrigState, screen_pitch) == 0x00502AD0u - 0x0048E000u, "screen_pitch");
static_assert(offsetof(struct AM2_OrigState, primary_surface) == 0x00502AD4u - 0x0048E000u, "primary_surface");
static_assert(offsetof(struct AM2_OrigState, colour_below_bg) == 0x00502AD8u - 0x0048E000u, "colour_below_bg");
static_assert(offsetof(struct AM2_OrigState, background_colour) == 0x00502AD9u - 0x0048E000u, "background_colour");
static_assert(offsetof(struct AM2_OrigState, default_palette_buf) == 0x00502BDCu - 0x0048E000u, "default_palette_buf");
static_assert(offsetof(struct AM2_OrigState, colour_dark_grey) == 0x00502CE4u - 0x0048E000u, "colour_dark_grey");
static_assert(offsetof(struct AM2_OrigState, colour_no_map) == 0x00502CE5u - 0x0048E000u, "colour_no_map");
static_assert(offsetof(struct AM2_OrigState, remap_shade_store) == 0x00502CECu - 0x0048E000u, "remap_shade_store");
static_assert(offsetof(struct AM2_OrigState, offscreen_surface) == 0x00503100u - 0x0048E000u, "offscreen_surface");
static_assert(offsetof(struct AM2_OrigState, tileset_palettes) == 0x00503108u - 0x0048E000u, "tileset_palettes");
static_assert(offsetof(struct AM2_OrigState, draw_target) == 0x00507128u - 0x0048E000u, "draw_target");
static_assert(offsetof(struct AM2_OrigState, kind7_names) == 0x0050712Cu - 0x0048E000u, "kind7_names");
static_assert(offsetof(struct AM2_OrigState, variation_table) == 0x00507130u - 0x0048E000u, "variation_table");
static_assert(offsetof(struct AM2_OrigState, remap_bright) == 0x00507230u - 0x0048E000u, "remap_bright");
static_assert(offsetof(struct AM2_OrigState, hud_message_colour) == 0x00507234u - 0x0048E000u, "hud_message_colour");
static_assert(offsetof(struct AM2_OrigState, remap_identity_buf) == 0x00507238u - 0x0048E000u, "remap_identity_buf");
static_assert(offsetof(struct AM2_OrigState, dd_clipper) == 0x00507340u - 0x0048E000u, "dd_clipper");
static_assert(offsetof(struct AM2_OrigState, opt_windowed) == 0x00507344u - 0x0048E000u, "opt_windowed");
static_assert(offsetof(struct AM2_OrigState, depth_cursor) == 0x00507348u - 0x0048E000u, "depth_cursor");
static_assert(offsetof(struct AM2_OrigState, depth_nodes) == 0x00507350u - 0x0048E000u, "depth_nodes");
static_assert(offsetof(struct AM2_OrigState, dirty_tail) == 0x00508AC0u - 0x0048E000u, "dirty_tail");
static_assert(offsetof(struct AM2_OrigState, dirty_rects) == 0x00508AC4u - 0x0048E000u, "dirty_rects");
static_assert(offsetof(struct AM2_OrigState, dirty_prev_head) == 0x00508AD4u - 0x0048E000u, "dirty_prev_head");
static_assert(offsetof(struct AM2_OrigState, dirty_head) == 0x00508AD6u - 0x0048E000u, "dirty_head");
static_assert(offsetof(struct AM2_OrigState, depth_count) == 0x0050B1D4u - 0x0048E000u, "depth_count");
static_assert(offsetof(struct AM2_OrigState, depth_head) == 0x0050B1D8u - 0x0048E000u, "depth_head");
static_assert(offsetof(struct AM2_OrigState, depth_field_dc) == 0x0050B1DCu - 0x0048E000u, "depth_field_dc");
static_assert(offsetof(struct AM2_OrigState, error_text_dd) == 0x0050B1E0u - 0x0048E000u, "error_text_dd");
static_assert(offsetof(struct AM2_OrigState, error_text) == 0x0050B5E0u - 0x0048E000u, "error_text");
static_assert(offsetof(struct AM2_OrigState, leak_total) == 0x0050C340u - 0x0048E000u, "leak_total");
static_assert(offsetof(struct AM2_OrigState, leak_count) == 0x0050C344u - 0x0048E000u, "leak_count");
static_assert(offsetof(struct AM2_OrigState, leak_records) == 0x0050C348u - 0x0048E000u, "leak_records");
static_assert(offsetof(struct AM2_OrigState, unread_50c34c) == 0x0050C34Cu - 0x0048E000u, "unread_50c34c");
static_assert(offsetof(struct AM2_OrigState, script_reloading) == 0x0050C350u - 0x0048E000u, "script_reloading");
static_assert(offsetof(struct AM2_OrigState, opt_trace_win) == 0x0050C354u - 0x0048E000u, "opt_trace_win");
static_assert(offsetof(struct AM2_OrigState, opt_dbg) == 0x0050C358u - 0x0048E000u, "opt_dbg");
static_assert(offsetof(struct AM2_OrigState, opt_trace_pf) == 0x0050C35Cu - 0x0048E000u, "opt_trace_pf");
static_assert(offsetof(struct AM2_OrigState, opt_trace_veh) == 0x0050C360u - 0x0048E000u, "opt_trace_veh");
static_assert(offsetof(struct AM2_OrigState, event_block) == 0x0050C368u - 0x0048E000u, "event_block");
static_assert(offsetof(struct AM2_OrigState, timer_count) == 0x0050C36Cu - 0x0048E000u, "timer_count");
static_assert(offsetof(struct AM2_OrigState, timer_table) == 0x0050C370u - 0x0048E000u, "timer_table");
static_assert(offsetof(struct AM2_OrigState, event_table) == 0x005101F0u - 0x0048E000u, "event_table");
static_assert(offsetof(struct AM2_OrigState, timer_table_id_end) == 0x005101FCu - 0x0048E000u, "timer_table_id_end");
static_assert(offsetof(struct AM2_OrigState, script_conditions) == 0x00510214u - 0x0048E000u, "script_conditions");
static_assert(offsetof(struct AM2_OrigState, rule_uid_a) == 0x00510218u - 0x0048E000u, "rule_uid_a");
static_assert(offsetof(struct AM2_OrigState, rule_uid_b) == 0x0051021Cu - 0x0048E000u, "rule_uid_b");
static_assert(offsetof(struct AM2_OrigState, rule_uid_c) == 0x00510220u - 0x0048E000u, "rule_uid_c");
static_assert(offsetof(struct AM2_OrigState, explosion_anims) == 0x00510228u - 0x0048E000u, "explosion_anims");
static_assert(offsetof(struct AM2_OrigState, sprite_set_shared) == 0x00510230u - 0x0048E000u, "sprite_set_shared");
static_assert(offsetof(struct AM2_OrigState, sprite_set_title) == 0x00510A40u - 0x0048E000u, "sprite_set_title");
static_assert(offsetof(struct AM2_OrigState, sprite_set_third) == 0x00511250u - 0x0048E000u, "sprite_set_third");
static_assert(offsetof(struct AM2_OrigState, state0_tick) == 0x00511A60u - 0x0048E000u, "state0_tick");
static_assert(offsetof(struct AM2_OrigState, gameproc_block) == 0x00511A68u - 0x0048E000u, "gameproc_block");
static_assert(offsetof(struct AM2_OrigState, map_name) == 0x00511A88u - 0x0048E000u, "map_name");
static_assert(offsetof(struct AM2_OrigState, map_folder) == 0x00511AC8u - 0x0048E000u, "map_folder");
static_assert(offsetof(struct AM2_OrigState, movie_to_play) == 0x00511B08u - 0x0048E000u, "movie_to_play");
static_assert(offsetof(struct AM2_OrigState, gameproc_str_b) == 0x00511B88u - 0x0048E000u, "gameproc_str_b");
static_assert(offsetof(struct AM2_OrigState, script_reload_path) == 0x00511BC8u - 0x0048E000u, "script_reload_path");
static_assert(offsetof(struct AM2_OrigState, mp_script_name) == 0x00511C08u - 0x0048E000u, "mp_script_name");
static_assert(offsetof(struct AM2_OrigState, level_str_a) == 0x00511C48u - 0x0048E000u, "level_str_a");
static_assert(offsetof(struct AM2_OrigState, level_str_b) == 0x00511C88u - 0x0048E000u, "level_str_b");
static_assert(offsetof(struct AM2_OrigState, tileset_reserve) == 0x00511CC8u - 0x0048E000u, "tileset_reserve");
static_assert(offsetof(struct AM2_OrigState, map_checksum_val) == 0x00511CCCu - 0x0048E000u, "map_checksum_val");
static_assert(offsetof(struct AM2_OrigState, mp_script_chksum_val) == 0x00511CD0u - 0x0048E000u, "mp_script_chksum_val");
static_assert(offsetof(struct AM2_OrigState, rules_checksum_val) == 0x00511CD4u - 0x0048E000u, "rules_checksum_val");
static_assert(offsetof(struct AM2_OrigState, level_str_d) == 0x00511CD8u - 0x0048E000u, "level_str_d");
static_assert(offsetof(struct AM2_OrigState, level_str_c) == 0x00511D18u - 0x0048E000u, "level_str_c");
static_assert(offsetof(struct AM2_OrigState, level_sound_name) == 0x00511D58u - 0x0048E000u, "level_sound_name");
static_assert(offsetof(struct AM2_OrigState, level_id) == 0x00511D98u - 0x0048E000u, "level_id");
static_assert(offsetof(struct AM2_OrigState, level_index) == 0x00511D9Cu - 0x0048E000u, "level_index");
static_assert(offsetof(struct AM2_OrigState, mp_session) == 0x00511DA0u - 0x0048E000u, "mp_session");
static_assert(offsetof(struct AM2_OrigState, game_state) == 0x00511DA4u - 0x0048E000u, "game_state");
static_assert(offsetof(struct AM2_OrigState, state_entered) == 0x00511DA8u - 0x0048E000u, "state_entered");
static_assert(offsetof(struct AM2_OrigState, state_pending) == 0x00511DACu - 0x0048E000u, "state_pending");
static_assert(offsetof(struct AM2_OrigState, state_wanted) == 0x00511DB0u - 0x0048E000u, "state_wanted");
static_assert(offsetof(struct AM2_OrigState, game_state_arg) == 0x00511DB4u - 0x0048E000u, "game_state_arg");
static_assert(offsetof(struct AM2_OrigState, menu_mode) == 0x00511DBCu - 0x0048E000u, "menu_mode");
static_assert(offsetof(struct AM2_OrigState, overlay_dirty) == 0x00511DC0u - 0x0048E000u, "overlay_dirty");
static_assert(offsetof(struct AM2_OrigState, menu_request_set) == 0x00511DC4u - 0x0048E000u, "menu_request_set");
static_assert(offsetof(struct AM2_OrigState, menu_request) == 0x00511DC8u - 0x0048E000u, "menu_request");
static_assert(offsetof(struct AM2_OrigState, script_reload) == 0x00511DCCu - 0x0048E000u, "script_reload");
static_assert(offsetof(struct AM2_OrigState, state_enter_once) == 0x00511DD0u - 0x0048E000u, "state_enter_once");
static_assert(offsetof(struct AM2_OrigState, net_game) == 0x00511DD4u - 0x0048E000u, "net_game");
static_assert(offsetof(struct AM2_OrigState, load_pending) == 0x00511DD8u - 0x0048E000u, "load_pending");
static_assert(offsetof(struct AM2_OrigState, have_default_cof) == 0x00511DDCu - 0x0048E000u, "have_default_cof");
static_assert(offsetof(struct AM2_OrigState, uid_counters) == 0x00511DE0u - 0x0048E000u, "uid_counters");
static_assert(offsetof(struct AM2_OrigState, next_uid) == 0x00511DF4u - 0x0048E000u, "next_uid");
static_assert(offsetof(struct AM2_OrigState, pad_count) == 0x00511DF8u - 0x0048E000u, "pad_count");
static_assert(offsetof(struct AM2_OrigState, script_state_flag) == 0x00511DFCu - 0x0048E000u, "script_state_flag");
static_assert(offsetof(struct AM2_OrigState, clock_base_ms) == 0x00511E00u - 0x0048E000u, "clock_base_ms");
static_assert(offsetof(struct AM2_OrigState, game_clock_ms) == 0x00511E04u - 0x0048E000u, "game_clock_ms");
static_assert(offsetof(struct AM2_OrigState, frame_delta_ms) == 0x00511E08u - 0x0048E000u, "frame_delta_ms");
static_assert(offsetof(struct AM2_OrigState, last_tick_ms) == 0x00511E0Cu - 0x0048E000u, "last_tick_ms");
static_assert(offsetof(struct AM2_OrigState, frame_delta_sec) == 0x00511E10u - 0x0048E000u, "frame_delta_sec");
static_assert(offsetof(struct AM2_OrigState, game_over_source) == 0x00511E14u - 0x0048E000u, "game_over_source");
static_assert(offsetof(struct AM2_OrigState, evt_id15_uid) == 0x00511E20u - 0x0048E000u, "evt_id15_uid");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_val_a) == 0x00511E24u - 0x0048E000u, "obj_ctx_val_a");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_val) == 0x00511E28u - 0x0048E000u, "obj_ctx_val");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_val_prev) == 0x00511E2Cu - 0x0048E000u, "obj_ctx_val_prev");
static_assert(offsetof(struct AM2_OrigState, level_flag_e30) == 0x00511E30u - 0x0048E000u, "level_flag_e30");
static_assert(offsetof(struct AM2_OrigState, view_snap) == 0x00511E34u - 0x0048E000u, "view_snap");
static_assert(offsetof(struct AM2_OrigState, view_hold) == 0x00511E38u - 0x0048E000u, "view_hold");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_set) == 0x00511E3Cu - 0x0048E000u, "obj_ctx_set");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_set_prev) == 0x00511E40u - 0x0048E000u, "obj_ctx_set_prev");
static_assert(offsetof(struct AM2_OrigState, input_suppress) == 0x00511E44u - 0x0048E000u, "input_suppress");
static_assert(offsetof(struct AM2_OrigState, evt_id15_flag) == 0x00511E48u - 0x0048E000u, "evt_id15_flag");
static_assert(offsetof(struct AM2_OrigState, our_leader_uid) == 0x00511E4Cu - 0x0048E000u, "our_leader_uid");
static_assert(offsetof(struct AM2_OrigState, leader_pos) == 0x00511E50u - 0x0048E000u, "leader_pos");
static_assert(offsetof(struct AM2_OrigState, leader_facing) == 0x00511E54u - 0x0048E000u, "leader_facing");
static_assert(offsetof(struct AM2_OrigState, weapon_owner_id) == 0x00511E58u - 0x0048E000u, "weapon_owner_id");
static_assert(offsetof(struct AM2_OrigState, weapon_slot) == 0x00511E5Cu - 0x0048E000u, "weapon_slot");
static_assert(offsetof(struct AM2_OrigState, ally_matrix) == 0x00511E60u - 0x0048E000u, "ally_matrix");
static_assert(offsetof(struct AM2_OrigState, message_text) == 0x00511EA4u - 0x0048E000u, "message_text");
static_assert(offsetof(struct AM2_OrigState, message_bmp_name) == 0x005122A4u - 0x0048E000u, "message_bmp_name");
static_assert(offsetof(struct AM2_OrigState, current_bitmap) == 0x005122C4u - 0x0048E000u, "current_bitmap");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_obj_a) == 0x005122C8u - 0x0048E000u, "obj_ctx_obj_a");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_obj) == 0x005122CCu - 0x0048E000u, "obj_ctx_obj");
static_assert(offsetof(struct AM2_OrigState, obj_ctx_obj_prev) == 0x005122D0u - 0x0048E000u, "obj_ctx_obj_prev");
static_assert(offsetof(struct AM2_OrigState, weapon_fn_slot0) == 0x005122D4u - 0x0048E000u, "weapon_fn_slot0");
static_assert(offsetof(struct AM2_OrigState, weapon_fn_slot1) == 0x005122D8u - 0x0048E000u, "weapon_fn_slot1");
static_assert(offsetof(struct AM2_OrigState, weapon_fn_slot3) == 0x005122DCu - 0x0048E000u, "weapon_fn_slot3");
static_assert(offsetof(struct AM2_OrigState, pointer_pick) == 0x005122E0u - 0x0048E000u, "pointer_pick");
static_assert(offsetof(struct AM2_OrigState, pointer_action) == 0x005122E4u - 0x0048E000u, "pointer_action");
static_assert(offsetof(struct AM2_OrigState, pointer_f10) == 0x005122E8u - 0x0048E000u, "pointer_f10");
static_assert(offsetof(struct AM2_OrigState, pointer_f14) == 0x005122ECu - 0x0048E000u, "pointer_f14");
static_assert(offsetof(struct AM2_OrigState, weapon_fn_slot2) == 0x005122F0u - 0x0048E000u, "weapon_fn_slot2");
static_assert(offsetof(struct AM2_OrigState, pointer_overlay) == 0x005122F4u - 0x0048E000u, "pointer_overlay");
static_assert(offsetof(struct AM2_OrigState, second_deadline) == 0x005122F8u - 0x0048E000u, "second_deadline");
static_assert(offsetof(struct AM2_OrigState, pause_flags) == 0x005122FCu - 0x0048E000u, "pause_flags");
static_assert(offsetof(struct AM2_OrigState, game_winner) == 0x00512300u - 0x0048E000u, "game_winner");
static_assert(offsetof(struct AM2_OrigState, win_enabled) == 0x00512304u - 0x0048E000u, "win_enabled");
static_assert(offsetof(struct AM2_OrigState, selected_uids) == 0x00512308u - 0x0048E000u, "selected_uids");
static_assert(offsetof(struct AM2_OrigState, selected_count) == 0x0051230Cu - 0x0048E000u, "selected_count");
static_assert(offsetof(struct AM2_OrigState, selected_items) == 0x00512310u - 0x0048E000u, "selected_items");
static_assert(offsetof(struct AM2_OrigState, game_seed) == 0x00512314u - 0x0048E000u, "game_seed");
static_assert(offsetof(struct AM2_OrigState, volume_at_zero) == 0x00512318u - 0x0048E000u, "volume_at_zero");
static_assert(offsetof(struct AM2_OrigState, stream_volume) == 0x0051231Cu - 0x0048E000u, "stream_volume");
static_assert(offsetof(struct AM2_OrigState, volume_voice) == 0x00512320u - 0x0048E000u, "volume_voice");
static_assert(offsetof(struct AM2_OrigState, difficulty) == 0x00512324u - 0x0048E000u, "difficulty");
static_assert(offsetof(struct AM2_OrigState, movie_count) == 0x00512328u - 0x0048E000u, "movie_count");
static_assert(offsetof(struct AM2_OrigState, mission_retry) == 0x0051232Cu - 0x0048E000u, "mission_retry");
static_assert(offsetof(struct AM2_OrigState, attempt_count) == 0x00512330u - 0x0048E000u, "attempt_count");
static_assert(offsetof(struct AM2_OrigState, startup_colours) == 0x00512340u - 0x0048E000u, "startup_colours");
static_assert(offsetof(struct AM2_OrigState, net_settle_count) == 0x0051234Cu - 0x0048E000u, "net_settle_count");
static_assert(offsetof(struct AM2_OrigState, perf_freq) == 0x00512350u - 0x0048E000u, "perf_freq");
static_assert(offsetof(struct AM2_OrigState, cheat_invulnerable) == 0x00512358u - 0x0048E000u, "cheat_invulnerable");
static_assert(offsetof(struct AM2_OrigState, game_dir) == 0x0051235Cu - 0x0048E000u, "game_dir");
static_assert(offsetof(struct AM2_OrigState, hwnd) == 0x0051245Cu - 0x0048E000u, "hwnd");
static_assert(offsetof(struct AM2_OrigState, full_redraw) == 0x00512460u - 0x0048E000u, "full_redraw");
static_assert(offsetof(struct AM2_OrigState, cd_path) == 0x00512464u - 0x0048E000u, "cd_path");
static_assert(offsetof(struct AM2_OrigState, aim_x) == 0x00512568u - 0x0048E000u, "aim_x");
static_assert(offsetof(struct AM2_OrigState, aim_y) == 0x0051256Au - 0x0048E000u, "aim_y");
static_assert(offsetof(struct AM2_OrigState, aim_z) == 0x0051256Cu - 0x0048E000u, "aim_z");
static_assert(offsetof(struct AM2_OrigState, perf_period) == 0x00512570u - 0x0048E000u, "perf_period");
static_assert(offsetof(struct AM2_OrigState, perf_start) == 0x00512578u - 0x0048E000u, "perf_start");
static_assert(offsetof(struct AM2_OrigState, hinstance) == 0x00512580u - 0x0048E000u, "hinstance");
static_assert(offsetof(struct AM2_OrigState, fast_machine) == 0x00512584u - 0x0048E000u, "fast_machine");
static_assert(offsetof(struct AM2_OrigState, cd_present) == 0x00512588u - 0x0048E000u, "cd_present");
static_assert(offsetof(struct AM2_OrigState, cd_found_flag) == 0x00512594u - 0x0048E000u, "cd_found_flag");
static_assert(offsetof(struct AM2_OrigState, fixed_step) == 0x00512598u - 0x0048E000u, "fixed_step");
static_assert(offsetof(struct AM2_OrigState, opt_no_movies) == 0x0051259Cu - 0x0048E000u, "opt_no_movies");
static_assert(offsetof(struct AM2_OrigState, zero_point) == 0x005125A0u - 0x0048E000u, "zero_point");
static_assert(offsetof(struct AM2_OrigState, zero_rect) == 0x005125A8u - 0x0048E000u, "zero_rect");
static_assert(offsetof(struct AM2_OrigState, char_handler) == 0x005125B8u - 0x0048E000u, "char_handler");
static_assert(offsetof(struct AM2_OrigState, slow_machine) == 0x005125C0u - 0x0048E000u, "slow_machine");
static_assert(offsetof(struct AM2_OrigState, opt_big_movies) == 0x005125C4u - 0x0048E000u, "opt_big_movies");
static_assert(offsetof(struct AM2_OrigState, keys_buffer_a) == 0x005125C8u - 0x0048E000u, "keys_buffer_a");
static_assert(offsetof(struct AM2_OrigState, keys_buffer_b) == 0x005126C8u - 0x0048E000u, "keys_buffer_b");
static_assert(offsetof(struct AM2_OrigState, keys_now_ptr) == 0x005127C8u - 0x0048E000u, "keys_now_ptr");
static_assert(offsetof(struct AM2_OrigState, keys_prev_ptr) == 0x005127CCu - 0x0048E000u, "keys_prev_ptr");
static_assert(offsetof(struct AM2_OrigState, key_repeat_at) == 0x005127D0u - 0x0048E000u, "key_repeat_at");
static_assert(offsetof(struct AM2_OrigState, key_pressed) == 0x00512BD0u - 0x0048E000u, "key_pressed");
static_assert(offsetof(struct AM2_OrigState, dinput) == 0x00512FD0u - 0x0048E000u, "dinput");
static_assert(offsetof(struct AM2_OrigState, di_mouse) == 0x00512FD4u - 0x0048E000u, "di_mouse");
static_assert(offsetof(struct AM2_OrigState, di_keyboard) == 0x00512FD8u - 0x0048E000u, "di_keyboard");
static_assert(offsetof(struct AM2_OrigState, di_device_3) == 0x00512FDCu - 0x0048E000u, "di_device_3");
static_assert(offsetof(struct AM2_OrigState, di_mouse_acquired) == 0x00512FE0u - 0x0048E000u, "di_mouse_acquired");
static_assert(offsetof(struct AM2_OrigState, item_header_size) == 0x0051307Cu - 0x0048E000u, "item_header_size");
static_assert(offsetof(struct AM2_OrigState, uid_remap_cap) == 0x00513080u - 0x0048E000u, "uid_remap_cap");
static_assert(offsetof(struct AM2_OrigState, uid_remap_count) == 0x00513084u - 0x0048E000u, "uid_remap_count");
static_assert(offsetof(struct AM2_OrigState, uid_remap) == 0x00513088u - 0x0048E000u, "uid_remap");
static_assert(offsetof(struct AM2_OrigState, iter_stamp) == 0x0051308Cu - 0x0048E000u, "iter_stamp");
static_assert(offsetof(struct AM2_OrigState, map_block) == 0x00514D90u - 0x0048E000u, "map_block");
static_assert(offsetof(struct AM2_OrigState, map_extent_x) == 0x00514DD0u - 0x0048E000u, "map_extent_x");
static_assert(offsetof(struct AM2_OrigState, map_extent_y) == 0x00514DD4u - 0x0048E000u, "map_extent_y");
static_assert(offsetof(struct AM2_OrigState, map_extent_shift) == 0x00514DD8u - 0x0048E000u, "map_extent_shift");
static_assert(offsetof(struct AM2_OrigState, map_tiles_w) == 0x00514DDCu - 0x0048E000u, "map_tiles_w");
static_assert(offsetof(struct AM2_OrigState, map_tiles_h) == 0x00514DE0u - 0x0048E000u, "map_tiles_h");
static_assert(offsetof(struct AM2_OrigState, map_row_shift) == 0x00514DE4u - 0x0048E000u, "map_row_shift");
static_assert(offsetof(struct AM2_OrigState, map_bounds) == 0x00514DE8u - 0x0048E000u, "map_bounds");
static_assert(offsetof(struct AM2_OrigState, map_bounds_left) == 0x00514DF8u - 0x0048E000u, "map_bounds_left");
static_assert(offsetof(struct AM2_OrigState, map_bounds_top) == 0x00514DFCu - 0x0048E000u, "map_bounds_top");
static_assert(offsetof(struct AM2_OrigState, map_bounds_right) == 0x00514E00u - 0x0048E000u, "map_bounds_right");
static_assert(offsetof(struct AM2_OrigState, map_bounds_bottom) == 0x00514E04u - 0x0048E000u, "map_bounds_bottom");
static_assert(offsetof(struct AM2_OrigState, view_target) == 0x00514E08u - 0x0048E000u, "view_target");
static_assert(offsetof(struct AM2_OrigState, listener_pos) == 0x00514E0Cu - 0x0048E000u, "listener_pos");
static_assert(offsetof(struct AM2_OrigState, listener_pos_prev) == 0x00514E10u - 0x0048E000u, "listener_pos_prev");
static_assert(offsetof(struct AM2_OrigState, view_origin_x) == 0x00514E14u - 0x0048E000u, "view_origin_x");
static_assert(offsetof(struct AM2_OrigState, view_origin_y) == 0x00514E18u - 0x0048E000u, "view_origin_y");
static_assert(offsetof(struct AM2_OrigState, view_far_x) == 0x00514E1Cu - 0x0048E000u, "view_far_x");
static_assert(offsetof(struct AM2_OrigState, view_far_y) == 0x00514E20u - 0x0048E000u, "view_far_y");
static_assert(offsetof(struct AM2_OrigState, view_rect_prev) == 0x00514E24u - 0x0048E000u, "view_rect_prev");
static_assert(offsetof(struct AM2_OrigState, second_rect) == 0x00514E34u - 0x0048E000u, "second_rect");
static_assert(offsetof(struct AM2_OrigState, second_rect_prev) == 0x00514E44u - 0x0048E000u, "second_rect_prev");
static_assert(offsetof(struct AM2_OrigState, view_clipped) == 0x00514E54u - 0x0048E000u, "view_clipped");
static_assert(offsetof(struct AM2_OrigState, shake_time) == 0x00514E64u - 0x0048E000u, "shake_time");
static_assert(offsetof(struct AM2_OrigState, shake_phase_x) == 0x00514E68u - 0x0048E000u, "shake_phase_x");
static_assert(offsetof(struct AM2_OrigState, shake_step_x) == 0x00514E6Cu - 0x0048E000u, "shake_step_x");
static_assert(offsetof(struct AM2_OrigState, shake_phase_y) == 0x00514E70u - 0x0048E000u, "shake_phase_y");
static_assert(offsetof(struct AM2_OrigState, shake_step_y) == 0x00514E74u - 0x0048E000u, "shake_step_y");
static_assert(offsetof(struct AM2_OrigState, shake_amplitude) == 0x00514E78u - 0x0048E000u, "shake_amplitude");
static_assert(offsetof(struct AM2_OrigState, map_surface) == 0x00514E90u - 0x0048E000u, "map_surface");
static_assert(offsetof(struct AM2_OrigState, map_cache_surface) == 0x00514E94u - 0x0048E000u, "map_cache_surface");
static_assert(offsetof(struct AM2_OrigState, tile_shift_log) == 0x00514E9Cu - 0x0048E000u, "tile_shift_log");
static_assert(offsetof(struct AM2_OrigState, view_tiles_w) == 0x00514EA0u - 0x0048E000u, "view_tiles_w");
static_assert(offsetof(struct AM2_OrigState, view_tiles_h) == 0x00514EA4u - 0x0048E000u, "view_tiles_h");
static_assert(offsetof(struct AM2_OrigState, visible_tiles) == 0x00514EA8u - 0x0048E000u, "visible_tiles");
static_assert(offsetof(struct AM2_OrigState, camera_y) == 0x00514EACu - 0x0048E000u, "camera_y");
static_assert(offsetof(struct AM2_OrigState, map_tiles) == 0x00514EB8u - 0x0048E000u, "map_tiles");
static_assert(offsetof(struct AM2_OrigState, tile_attrs) == 0x00514EBCu - 0x0048E000u, "tile_attrs");
static_assert(offsetof(struct AM2_OrigState, cell_weights) == 0x00514EC0u - 0x0048E000u, "cell_weights");
static_assert(offsetof(struct AM2_OrigState, map_padbit_layer) == 0x00514EC4u - 0x0048E000u, "map_padbit_layer");
static_assert(offsetof(struct AM2_OrigState, map_pad_layer) == 0x00514EC8u - 0x0048E000u, "map_pad_layer");
static_assert(offsetof(struct AM2_OrigState, region_of_cell) == 0x00514ECCu - 0x0048E000u, "region_of_cell");
static_assert(offsetof(struct AM2_OrigState, tile_flags) == 0x00514ED0u - 0x0048E000u, "tile_flags");
static_assert(offsetof(struct AM2_OrigState, tile_kind) == 0x00514ED4u - 0x0048E000u, "tile_kind");
static_assert(offsetof(struct AM2_OrigState, tile_reveal_grids) == 0x00514ED8u - 0x0048E000u, "tile_reveal_grids");
static_assert(offsetof(struct AM2_OrigState, tile_cover) == 0x00514EE8u - 0x0048E000u, "tile_cover");
static_assert(offsetof(struct AM2_OrigState, region_stride) == 0x00514EECu - 0x0048E000u, "region_stride");
static_assert(offsetof(struct AM2_OrigState, regions) == 0x00514EF0u - 0x0048E000u, "regions");
static_assert(offsetof(struct AM2_OrigState, region_next) == 0x00514EF4u - 0x0048E000u, "region_next");
static_assert(offsetof(struct AM2_OrigState, region_stamp) == 0x00514EF8u - 0x0048E000u, "region_stamp");
static_assert(offsetof(struct AM2_OrigState, region_cost) == 0x00514EFCu - 0x0048E000u, "region_cost");
static_assert(offsetof(struct AM2_OrigState, obj_capacity) == 0x00514F00u - 0x0048E000u, "obj_capacity");
static_assert(offsetof(struct AM2_OrigState, obj_count) == 0x00514F04u - 0x0048E000u, "obj_count");
static_assert(offsetof(struct AM2_OrigState, iter_cursor) == 0x00514F08u - 0x0048E000u, "iter_cursor");
static_assert(offsetof(struct AM2_OrigState, obj_table) == 0x00514F0Cu - 0x0048E000u, "obj_table");
static_assert(offsetof(struct AM2_OrigState, obj_map_desc) == 0x00514F10u - 0x0048E000u, "obj_map_desc");
static_assert(offsetof(struct AM2_OrigState, map_desc) == 0x00514F20u - 0x0048E000u, "map_desc");
static_assert(offsetof(struct AM2_OrigState, palette_cycle_stamp) == 0x00514F70u - 0x0048E000u, "palette_cycle_stamp");
static_assert(offsetof(struct AM2_OrigState, trig_sin) == 0x00514F80u - 0x0048E000u, "trig_sin");
static_assert(offsetof(struct AM2_OrigState, trig_atan_sin) == 0x00515580u - 0x0048E000u, "trig_atan_sin");
static_assert(offsetof(struct AM2_OrigState, trig_cos) == 0x00515784u - 0x0048E000u, "trig_cos");
static_assert(offsetof(struct AM2_OrigState, trig_atan_cos) == 0x00515D84u - 0x0048E000u, "trig_atan_cos");
static_assert(offsetof(struct AM2_OrigState, game_over_state) == 0x00515F88u - 0x0048E000u, "game_over_state");
static_assert(offsetof(struct AM2_OrigState, game_over_saved) == 0x00515F8Cu - 0x0048E000u, "game_over_saved");
static_assert(offsetof(struct AM2_OrigState, state_movie) == 0x00515F98u - 0x0048E000u, "state_movie");
static_assert(offsetof(struct AM2_OrigState, mp_panel_sprites_b) == 0x00515FA0u - 0x0048E000u, "mp_panel_sprites_b");
static_assert(offsetof(struct AM2_OrigState, mp_panel_sprites_b_end) == 0x00515FD4u - 0x0048E000u, "mp_panel_sprites_b_end");
static_assert(offsetof(struct AM2_OrigState, game_over_flags) == 0x00515FD8u - 0x0048E000u, "game_over_flags");
static_assert(offsetof(struct AM2_OrigState, game_setting_22c) == 0x00515FDCu - 0x0048E000u, "game_setting_22c");
static_assert(offsetof(struct AM2_OrigState, army_points) == 0x00515FE0u - 0x0048E000u, "army_points");
static_assert(offsetof(struct AM2_OrigState, score_limit) == 0x00515FF0u - 0x0048E000u, "score_limit");
static_assert(offsetof(struct AM2_OrigState, live_player_name) == 0x00515FF4u - 0x0048E000u, "live_player_name");
static_assert(offsetof(struct AM2_OrigState, live_battle_name) == 0x00516035u - 0x0048E000u, "live_battle_name");
static_assert(offsetof(struct AM2_OrigState, host_mask_a) == 0x00516078u - 0x0048E000u, "host_mask_a");
static_assert(offsetof(struct AM2_OrigState, host_mask_b) == 0x0051607Cu - 0x0048E000u, "host_mask_b");
static_assert(offsetof(struct AM2_OrigState, host_value_3e8) == 0x00516090u - 0x0048E000u, "host_value_3e8");
static_assert(offsetof(struct AM2_OrigState, saved_player_name) == 0x00516094u - 0x0048E000u, "saved_player_name");
static_assert(offsetof(struct AM2_OrigState, saved_battle_name) == 0x005160D5u - 0x0048E000u, "saved_battle_name");
static_assert(offsetof(struct AM2_OrigState, mp_panel_sprites_a) == 0x00516118u - 0x0048E000u, "mp_panel_sprites_a");
static_assert(offsetof(struct AM2_OrigState, menu_msg_list) == 0x0051612Cu - 0x0048E000u, "menu_msg_list");
static_assert(offsetof(struct AM2_OrigState, session_object) == 0x00516130u - 0x0048E000u, "session_object");
static_assert(offsetof(struct AM2_OrigState, record_list_cap) == 0x00516138u - 0x0048E000u, "record_list_cap");
static_assert(offsetof(struct AM2_OrigState, record_list_count) == 0x0051613Cu - 0x0048E000u, "record_list_count");
static_assert(offsetof(struct AM2_OrigState, record_lists) == 0x00516140u - 0x0048E000u, "record_lists");
static_assert(offsetof(struct AM2_OrigState, aai_record_cap) == 0x00516144u - 0x0048E000u, "aai_record_cap");
static_assert(offsetof(struct AM2_OrigState, key_table_count) == 0x00516148u - 0x0048E000u, "key_table_count");
static_assert(offsetof(struct AM2_OrigState, aai_records) == 0x0051614Cu - 0x0048E000u, "aai_records");
static_assert(offsetof(struct AM2_OrigState, key_table) == 0x00516150u - 0x0048E000u, "key_table");
static_assert(offsetof(struct AM2_OrigState, record_list_index) == 0x00516154u - 0x0048E000u, "record_list_index");
static_assert(offsetof(struct AM2_OrigState, aai_key_980000) == 0x00516158u - 0x0048E000u, "aai_key_980000");
static_assert(offsetof(struct AM2_OrigState, aai_key_980080) == 0x0051615Cu - 0x0048E000u, "aai_key_980080");
static_assert(offsetof(struct AM2_OrigState, create_watched_kind) == 0x00516160u - 0x0048E000u, "create_watched_kind");
static_assert(offsetof(struct AM2_OrigState, watched_type_id) == 0x00516164u - 0x0048E000u, "watched_type_id");
static_assert(offsetof(struct AM2_OrigState, aai_key_980100) == 0x00516168u - 0x0048E000u, "aai_key_980100");
static_assert(offsetof(struct AM2_OrigState, kind7_count) == 0x0051616Cu - 0x0048E000u, "kind7_count");
static_assert(offsetof(struct AM2_OrigState, def_obj_recs) == 0x00516170u - 0x0048E000u, "def_obj_recs");
static_assert(offsetof(struct AM2_OrigState, def_obj_rec_count) == 0x00516174u - 0x0048E000u, "def_obj_rec_count");
static_assert(offsetof(struct AM2_OrigState, def_obj_rec_cap) == 0x00516178u - 0x0048E000u, "def_obj_rec_cap");
static_assert(offsetof(struct AM2_OrigState, def_links) == 0x0051617Cu - 0x0048E000u, "def_links");
static_assert(offsetof(struct AM2_OrigState, def_link_count) == 0x00516180u - 0x0048E000u, "def_link_count");
static_assert(offsetof(struct AM2_OrigState, def_link_cap) == 0x00516184u - 0x0048E000u, "def_link_cap");
static_assert(offsetof(struct AM2_OrigState, obj_scripts) == 0x00516188u - 0x0048E000u, "obj_scripts");
static_assert(offsetof(struct AM2_OrigState, current_obj_script) == 0x0051618Cu - 0x0048E000u, "current_obj_script");
static_assert(offsetof(struct AM2_OrigState, obj_script_cap) == 0x00516190u - 0x0048E000u, "obj_script_cap");
static_assert(offsetof(struct AM2_OrigState, pads) == 0x00516198u - 0x0048E000u, "pads");
static_assert(offsetof(struct AM2_OrigState, pad_numbers) == 0x0051F198u - 0x0048E000u, "pad_numbers");
static_assert(offsetof(struct AM2_OrigState, region_insert_prev) == 0x00523D98u - 0x0048E000u, "region_insert_prev");
static_assert(offsetof(struct AM2_OrigState, region_neighbour) == 0x00523D9Cu - 0x0048E000u, "region_neighbour");
static_assert(offsetof(struct AM2_OrigState, tile_step8) == 0x00523DA0u - 0x0048E000u, "tile_step8");
static_assert(offsetof(struct AM2_OrigState, region_goal) == 0x00523DC0u - 0x0048E000u, "region_goal");
static_assert(offsetof(struct AM2_OrigState, region_open_head) == 0x00523DC4u - 0x0048E000u, "region_open_head");
static_assert(offsetof(struct AM2_OrigState, path_resume_head) == 0x00523DC8u - 0x0048E000u, "path_resume_head");
static_assert(offsetof(struct AM2_OrigState, region_resume_goalid) == 0x00523DCCu - 0x0048E000u, "region_resume_goalid");
static_assert(offsetof(struct AM2_OrigState, region_walk) == 0x00523DD4u - 0x0048E000u, "region_walk");
static_assert(offsetof(struct AM2_OrigState, point_rule_army) == 0x00523DD8u - 0x0048E000u, "point_rule_army");
static_assert(offsetof(struct AM2_OrigState, point_rule) == 0x00523DDCu - 0x0048E000u, "point_rule");
static_assert(offsetof(struct AM2_OrigState, tile_line_buf) == 0x00523DE0u - 0x0048E000u, "tile_line_buf");
static_assert(offsetof(struct AM2_OrigState, tile_ring8) == 0x0053C480u - 0x0048E000u, "tile_ring8");
static_assert(offsetof(struct AM2_OrigState, region_resume_node) == 0x0053C4C8u - 0x0048E000u, "region_resume_node");
static_assert(offsetof(struct AM2_OrigState, tile_ring4) == 0x00554B70u - 0x0048E000u, "tile_ring4");
static_assert(offsetof(struct AM2_OrigState, path_resume_goal) == 0x00554B80u - 0x0048E000u, "path_resume_goal");
static_assert(offsetof(struct AM2_OrigState, tilemask_neighbours) == 0x00554B84u - 0x0048E000u, "tilemask_neighbours");
static_assert(offsetof(struct AM2_OrigState, path_nodes) == 0x00554BD8u - 0x0048E000u, "path_nodes");
static_assert(offsetof(struct AM2_OrigState, tile_neighbours) == 0x00654BD8u - 0x0048E000u, "tile_neighbours");
static_assert(offsetof(struct AM2_OrigState, region_current) == 0x00654C28u - 0x0048E000u, "region_current");
static_assert(offsetof(struct AM2_OrigState, region_generation) == 0x00654C2Cu - 0x0048E000u, "region_generation");
static_assert(offsetof(struct AM2_OrigState, path_generation) == 0x00654C30u - 0x0048E000u, "path_generation");
static_assert(offsetof(struct AM2_OrigState, path_frame_ms) == 0x00654C34u - 0x0048E000u, "path_frame_ms");
static_assert(offsetof(struct AM2_OrigState, path_frame_stamp) == 0x00654C38u - 0x0048E000u, "path_frame_stamp");
static_assert(offsetof(struct AM2_OrigState, placements) == 0x00654C7Cu - 0x0048E000u, "placements");
static_assert(offsetof(struct AM2_OrigState, placement_count) == 0x00654C80u - 0x0048E000u, "placement_count");
static_assert(offsetof(struct AM2_OrigState, placement_cap) == 0x00654C84u - 0x0048E000u, "placement_cap");
static_assert(offsetof(struct AM2_OrigState, missile_anims) == 0x00654C90u - 0x0048E000u, "missile_anims");
static_assert(offsetof(struct AM2_OrigState, roach_anims) == 0x00654C98u - 0x0048E000u, "roach_anims");
static_assert(offsetof(struct AM2_OrigState, roach_mask_directions) == 0x00654CA0u - 0x0048E000u, "roach_mask_directions");
static_assert(offsetof(struct AM2_OrigState, roach_mask_count) == 0x00654CA8u - 0x0048E000u, "roach_mask_count");
static_assert(offsetof(struct AM2_OrigState, roach_mask) == 0x00654CACu - 0x0048E000u, "roach_mask");
static_assert(offsetof(struct AM2_OrigState, roach_mark) == 0x00656128u - 0x0048E000u, "roach_mark");
static_assert(offsetof(struct AM2_OrigState, roach_mark_stamp) == 0x00656328u - 0x0048E000u, "roach_mark_stamp");
static_assert(offsetof(struct AM2_OrigState, scenario_unread) == 0x00656330u - 0x0048E000u, "scenario_unread");
static_assert(offsetof(struct AM2_OrigState, scenario_count) == 0x00656332u - 0x0048E000u, "scenario_count");
static_assert(offsetof(struct AM2_OrigState, scenarios) == 0x00656334u - 0x0048E000u, "scenarios");
static_assert(offsetof(struct AM2_OrigState, level_table) == 0x00656338u - 0x0048E000u, "level_table");
static_assert(offsetof(struct AM2_OrigState, level_table_count) == 0x0065633Cu - 0x0048E000u, "level_table_count");
static_assert(offsetof(struct AM2_OrigState, level_table_cap) == 0x00656340u - 0x0048E000u, "level_table_cap");
static_assert(offsetof(struct AM2_OrigState, name_table_base) == 0x00656344u - 0x0048E000u, "name_table_base");
static_assert(offsetof(struct AM2_OrigState, name_table_count) == 0x00656348u - 0x0048E000u, "name_table_count");
static_assert(offsetof(struct AM2_OrigState, name_table_cap) == 0x0065634Cu - 0x0048E000u, "name_table_cap");
static_assert(offsetof(struct AM2_OrigState, svar_numgreen) == 0x00656350u - 0x0048E000u, "svar_numgreen");
static_assert(offsetof(struct AM2_OrigState, script_word_buf) == 0x00656354u - 0x0048E000u, "script_word_buf");
static_assert(offsetof(struct AM2_OrigState, svar_blue) == 0x00656454u - 0x0048E000u, "svar_blue");
static_assert(offsetof(struct AM2_OrigState, svar_me) == 0x00656458u - 0x0048E000u, "svar_me");
static_assert(offsetof(struct AM2_OrigState, script_name_cap) == 0x00656460u - 0x0048E000u, "script_name_cap");
static_assert(offsetof(struct AM2_OrigState, script_name_count) == 0x00656464u - 0x0048E000u, "script_name_count");
static_assert(offsetof(struct AM2_OrigState, script_names) == 0x00656468u - 0x0048E000u, "script_names");
static_assert(offsetof(struct AM2_OrigState, svar_grey) == 0x0065646Cu - 0x0048E000u, "svar_grey");
static_assert(offsetof(struct AM2_OrigState, svar_systemspeed) == 0x00656470u - 0x0048E000u, "svar_systemspeed");
static_assert(offsetof(struct AM2_OrigState, svar_id15) == 0x00656474u - 0x0048E000u, "svar_id15");
static_assert(offsetof(struct AM2_OrigState, script_context) == 0x00656478u - 0x0048E000u, "script_context");
static_assert(offsetof(struct AM2_OrigState, svar_green) == 0x00656484u - 0x0048E000u, "svar_green");
static_assert(offsetof(struct AM2_OrigState, svar_greenscore) == 0x00656488u - 0x0048E000u, "svar_greenscore");
static_assert(offsetof(struct AM2_OrigState, svar_tanscore) == 0x0065648Cu - 0x0048E000u, "svar_tanscore");
static_assert(offsetof(struct AM2_OrigState, svar_bluescore) == 0x00656490u - 0x0048E000u, "svar_bluescore");
static_assert(offsetof(struct AM2_OrigState, svar_greyscore) == 0x00656494u - 0x0048E000u, "svar_greyscore");
static_assert(offsetof(struct AM2_OrigState, svar_tan) == 0x00656498u - 0x0048E000u, "svar_tan");
static_assert(offsetof(struct AM2_OrigState, svar_difficulty) == 0x0065649Cu - 0x0048E000u, "svar_difficulty");
static_assert(offsetof(struct AM2_OrigState, system_palette) == 0x006564A0u - 0x0048E000u, "system_palette");
static_assert(offsetof(struct AM2_OrigState, movie_current) == 0x006568A0u - 0x0048E000u, "movie_current");
static_assert(offsetof(struct AM2_OrigState, movie_sound_ready) == 0x006598A8u - 0x0048E000u, "movie_sound_ready");
static_assert(offsetof(struct AM2_OrigState, sprite_file) == 0x006598B8u - 0x0048E000u, "sprite_file");
static_assert(offsetof(struct AM2_OrigState, sprite_reg_cap) == 0x006598BCu - 0x0048E000u, "sprite_reg_cap");
static_assert(offsetof(struct AM2_OrigState, sprite_reg_count) == 0x006598C0u - 0x0048E000u, "sprite_reg_count");
static_assert(offsetof(struct AM2_OrigState, sprite_table) == 0x006598C4u - 0x0048E000u, "sprite_table");
static_assert(offsetof(struct AM2_OrigState, sprite_reg_pairs) == 0x006598C8u - 0x0048E000u, "sprite_reg_pairs");
static_assert(offsetof(struct AM2_OrigState, glyph_size) == 0x006598D0u - 0x0048E000u, "glyph_size");
static_assert(offsetof(struct AM2_OrigState, glyph_offsets) == 0x006598D4u - 0x0048E000u, "glyph_offsets");
static_assert(offsetof(struct AM2_OrigState, glyph_offset_space) == 0x00659914u - 0x0048E000u, "glyph_offset_space");
static_assert(offsetof(struct AM2_OrigState, font_bases) == 0x00659AD4u - 0x0048E000u, "font_bases");
static_assert(offsetof(struct AM2_OrigState, throttle_deadline) == 0x00659EF8u - 0x0048E000u, "throttle_deadline");
static_assert(offsetof(struct AM2_OrigState, soldier_anims) == 0x00659F00u - 0x0048E000u, "soldier_anims");
static_assert(offsetof(struct AM2_OrigState, turn_repeat_ms) == 0x00659F48u - 0x0048E000u, "turn_repeat_ms");
static_assert(offsetof(struct AM2_OrigState, def_trooper_recs) == 0x00659F4Cu - 0x0048E000u, "def_trooper_recs");
static_assert(offsetof(struct AM2_OrigState, def_trooper_count) == 0x00659F50u - 0x0048E000u, "def_trooper_count");
static_assert(offsetof(struct AM2_OrigState, def_trooper_cap) == 0x00659F54u - 0x0048E000u, "def_trooper_cap");
static_assert(offsetof(struct AM2_OrigState, pending_confirm) == 0x00659F58u - 0x0048E000u, "pending_confirm");
static_assert(offsetof(struct AM2_OrigState, paint_object) == 0x0065A058u - 0x0048E000u, "paint_object");
static_assert(offsetof(struct AM2_OrigState, focused_edit) == 0x0065A05Cu - 0x0048E000u, "focused_edit");
static_assert(offsetof(struct AM2_OrigState, movie_page) == 0x0065A060u - 0x0048E000u, "movie_page");
static_assert(offsetof(struct AM2_OrigState, slot_headings) == 0x0065A068u - 0x0048E000u, "slot_headings");
static_assert(offsetof(struct AM2_OrigState, slot_positions) == 0x0065A0A8u - 0x0048E000u, "slot_positions");
static_assert(offsetof(struct AM2_OrigState, slot_is_vehicle) == 0x0065A1A8u - 0x0048E000u, "slot_is_vehicle");
static_assert(offsetof(struct AM2_OrigState, turret_anims) == 0x0065A2A8u - 0x0048E000u, "turret_anims");
static_assert(offsetof(struct AM2_OrigState, vehicle_mask_directions) == 0x0065A2D8u - 0x0048E000u, "vehicle_mask_directions");
static_assert(offsetof(struct AM2_OrigState, vehicle_mask_count) == 0x0065A2F0u - 0x0048E000u, "vehicle_mask_count");
static_assert(offsetof(struct AM2_OrigState, vehicle_mask) == 0x0065A2F4u - 0x0048E000u, "vehicle_mask");
static_assert(offsetof(struct AM2_OrigState, vehicle_anims) == 0x00661DF0u - 0x0048E000u, "vehicle_anims");
static_assert(offsetof(struct AM2_OrigState, obj_mark) == 0x00661E20u - 0x0048E000u, "obj_mark");
static_assert(offsetof(struct AM2_OrigState, obj_mark_stamp) == 0x00662020u - 0x0048E000u, "obj_mark_stamp");
static_assert(offsetof(struct AM2_OrigState, vehicle_defs) == 0x00662024u - 0x0048E000u, "vehicle_defs");
static_assert(offsetof(struct AM2_OrigState, vehicle_def_count) == 0x00662028u - 0x0048E000u, "vehicle_def_count");
static_assert(offsetof(struct AM2_OrigState, vehicle_def_cap) == 0x0066202Cu - 0x0048E000u, "vehicle_def_cap");
static_assert(offsetof(struct AM2_OrigState, missile_defs) == 0x00662030u - 0x0048E000u, "missile_defs");
static_assert(offsetof(struct AM2_OrigState, spawn_kind_table) == 0x0066205Cu - 0x0048E000u, "spawn_kind_table");
static_assert(offsetof(struct AM2_OrigState, unused_662288) == 0x00662288u - 0x0048E000u, "unused_662288");
static_assert(offsetof(struct AM2_OrigState, spawn_extra_6622bc) == 0x006622BCu - 0x0048E000u, "spawn_extra_6622bc");
static_assert(offsetof(struct AM2_OrigState, pick_reach_662450) == 0x00662450u - 0x0048E000u, "pick_reach_662450");
static_assert(offsetof(struct AM2_OrigState, pick_reach_6624ec) == 0x006624ECu - 0x0048E000u, "pick_reach_6624ec");
static_assert(offsetof(struct AM2_OrigState, medic_heal_pct) == 0x006624F8u - 0x0048E000u, "medic_heal_pct");
static_assert(offsetof(struct AM2_OrigState, pick_reach_66275c) == 0x0066275Cu - 0x0048E000u, "pick_reach_66275c");
static_assert(offsetof(struct AM2_OrigState, aim_life_half_a) == 0x00662820u - 0x0048E000u, "aim_life_half_a");
static_assert(offsetof(struct AM2_OrigState, aim_damage) == 0x00662838u - 0x0048E000u, "aim_damage");
static_assert(offsetof(struct AM2_OrigState, aim_life_half_b) == 0x00662854u - 0x0048E000u, "aim_life_half_b");
static_assert(offsetof(struct AM2_OrigState, aim_spawn_arg) == 0x0066286Cu - 0x0048E000u, "aim_spawn_arg");
static_assert(offsetof(struct AM2_OrigState, pick_reach_662894) == 0x00662894u - 0x0048E000u, "pick_reach_662894");
static_assert(offsetof(struct AM2_OrigState, repair_heal_pct) == 0x006628A0u - 0x0048E000u, "repair_heal_pct");
static_assert(offsetof(struct AM2_OrigState, pick_reach_6628c8) == 0x006628C8u - 0x0048E000u, "pick_reach_6628c8");
static_assert(offsetof(struct AM2_OrigState, spawn_extra_6628d4) == 0x006628D4u - 0x0048E000u, "spawn_extra_6628d4");
static_assert(offsetof(struct AM2_OrigState, sweep_distance) == 0x006628FCu - 0x0048E000u, "sweep_distance");
static_assert(offsetof(struct AM2_OrigState, sweep_damage) == 0x00662908u - 0x0048E000u, "sweep_damage");
static_assert(offsetof(struct AM2_OrigState, respawn_kinds) == 0x00662920u - 0x0048E000u, "respawn_kinds");
static_assert(offsetof(struct AM2_OrigState, respawn_kind_count) == 0x00662924u - 0x0048E000u, "respawn_kind_count");
static_assert(offsetof(struct AM2_OrigState, def_missile_recs) == 0x00662928u - 0x0048E000u, "def_missile_recs");
static_assert(offsetof(struct AM2_OrigState, def_missile_count) == 0x0066292Cu - 0x0048E000u, "def_missile_count");
static_assert(offsetof(struct AM2_OrigState, def_missile_cap) == 0x00662930u - 0x0048E000u, "def_missile_cap");
static_assert(offsetof(struct AM2_OrigState, rowpool_a_count) == 0x00662938u - 0x0048E000u, "rowpool_a_count");
static_assert(offsetof(struct AM2_OrigState, rowpool_a_tail) == 0x0066293Cu - 0x0048E000u, "rowpool_a_tail");
static_assert(offsetof(struct AM2_OrigState, rowpool_a_entries) == 0x00662940u - 0x0048E000u, "rowpool_a_entries");
static_assert(offsetof(struct AM2_OrigState, spawn_kind_table_end) == 0x0066294Cu - 0x0048E000u, "spawn_kind_table_end");
static_assert(offsetof(struct AM2_OrigState, seq_ctx_b) == 0x006640B0u - 0x0048E000u, "seq_ctx_b");
static_assert(offsetof(struct AM2_OrigState, rowpool_b_count) == 0x006640C8u - 0x0048E000u, "rowpool_b_count");
static_assert(offsetof(struct AM2_OrigState, rowpool_b_tail) == 0x006640CCu - 0x0048E000u, "rowpool_b_tail");
static_assert(offsetof(struct AM2_OrigState, rowpool_b_entries) == 0x006640D0u - 0x0048E000u, "rowpool_b_entries");
static_assert(offsetof(struct AM2_OrigState, seq_ctx_a) == 0x00664580u - 0x0048E000u, "seq_ctx_a");
static_assert(offsetof(struct AM2_OrigState, mp_mark_a) == 0x00664594u - 0x0048E000u, "mp_mark_a");
static_assert(offsetof(struct AM2_OrigState, mp_mark_b) == 0x00664598u - 0x0048E000u, "mp_mark_b");
static_assert(offsetof(struct AM2_OrigState, mp_mark_c) == 0x0066459Cu - 0x0048E000u, "mp_mark_c");
static_assert(offsetof(struct AM2_OrigState, mp_team_sprites) == 0x006645A0u - 0x0048E000u, "mp_team_sprites");
static_assert(offsetof(struct AM2_OrigState, startup_colours_b) == 0x006645A4u - 0x0048E000u, "startup_colours_b");
static_assert(offsetof(struct AM2_OrigState, crt_adjust_fdiv) == 0x006645ACu - 0x0048E000u, "crt_adjust_fdiv");
static_assert(offsetof(struct AM2_OrigState, crt_time_dst_cache) == 0x006645B0u - 0x0048E000u, "crt_time_dst_cache");
static_assert(offsetof(struct AM2_OrigState, crt_time_systime_cache) == 0x006645B8u - 0x0048E000u, "crt_time_systime_cache");
static_assert(offsetof(struct AM2_OrigState, crt_strtok_next) == 0x006645C8u - 0x0048E000u, "crt_strtok_next");
static_assert(offsetof(struct AM2_OrigState, crt_aenvptr) == 0x006645CCu - 0x0048E000u, "crt_aenvptr");
static_assert(offsetof(struct AM2_OrigState, crt_error_mode) == 0x006645D4u - 0x0048E000u, "crt_error_mode");
static_assert(offsetof(struct AM2_OrigState, crt_cftog_pflt) == 0x006645D8u - 0x0048E000u, "crt_cftog_pflt");
static_assert(offsetof(struct AM2_OrigState, crt_cftog_active) == 0x006645DCu - 0x0048E000u, "crt_cftog_active");
static_assert(offsetof(struct AM2_OrigState, crt_cftog_magnitude) == 0x006645E0u - 0x0048E000u, "crt_cftog_magnitude");
static_assert(offsetof(struct AM2_OrigState, crt_cftog_expansion) == 0x006645E4u - 0x0048E000u, "crt_cftog_expansion");
static_assert(offsetof(struct AM2_OrigState, crt_pnhheap) == 0x006645E8u - 0x0048E000u, "crt_pnhheap");
static_assert(offsetof(struct AM2_OrigState, crt_newmode) == 0x006645ECu - 0x0048E000u, "crt_newmode");
static_assert(offsetof(struct AM2_OrigState, crt_errno) == 0x00664600u - 0x0048E000u, "crt_errno");
static_assert(offsetof(struct AM2_OrigState, crt_doserrno) == 0x00664604u - 0x0048E000u, "crt_doserrno");
static_assert(offsetof(struct AM2_OrigState, crt_umaskval) == 0x00664608u - 0x0048E000u, "crt_umaskval");
static_assert(offsetof(struct AM2_OrigState, crt_osver) == 0x0066460Cu - 0x0048E000u, "crt_osver");
static_assert(offsetof(struct AM2_OrigState, crt_winver) == 0x00664610u - 0x0048E000u, "crt_winver");
static_assert(offsetof(struct AM2_OrigState, crt_winmajor) == 0x00664614u - 0x0048E000u, "crt_winmajor");
static_assert(offsetof(struct AM2_OrigState, crt_winminor) == 0x00664618u - 0x0048E000u, "crt_winminor");
static_assert(offsetof(struct AM2_OrigState, crt_argc) == 0x0066461Cu - 0x0048E000u, "crt_argc");
static_assert(offsetof(struct AM2_OrigState, crt_argv) == 0x00664620u - 0x0048E000u, "crt_argv");
static_assert(offsetof(struct AM2_OrigState, crt_environ) == 0x00664628u - 0x0048E000u, "crt_environ");
static_assert(offsetof(struct AM2_OrigState, crt_wenviron) == 0x00664630u - 0x0048E000u, "crt_wenviron");
static_assert(offsetof(struct AM2_OrigState, crt_pgmptr) == 0x00664638u - 0x0048E000u, "crt_pgmptr");
static_assert(offsetof(struct AM2_OrigState, crt_exit_retcaller) == 0x00664640u - 0x0048E000u, "crt_exit_retcaller");
static_assert(offsetof(struct AM2_OrigState, crt_exit_started) == 0x00664644u - 0x0048E000u, "crt_exit_started");
static_assert(offsetof(struct AM2_OrigState, crt_exit_done) == 0x00664648u - 0x0048E000u, "crt_exit_done");
static_assert(offsetof(struct AM2_OrigState, crt_lc_handle_ctype) == 0x00664674u - 0x0048E000u, "crt_lc_handle_ctype");
static_assert(offsetof(struct AM2_OrigState, crt_lc_codepage) == 0x00664684u - 0x0048E000u, "crt_lc_codepage");
static_assert(offsetof(struct AM2_OrigState, crt_cflush) == 0x0066468Cu - 0x0048E000u, "crt_cflush");
static_assert(offsetof(struct AM2_OrigState, crt_pgmname) == 0x0066469Cu - 0x0048E000u, "crt_pgmname");
static_assert(offsetof(struct AM2_OrigState, crt_env_kind) == 0x006647A0u - 0x0048E000u, "crt_env_kind");
static_assert(offsetof(struct AM2_OrigState, crt_msgbanner_hook) == 0x006647A4u - 0x0048E000u, "crt_msgbanner_hook");
static_assert(offsetof(struct AM2_OrigState, crt_fltout_raw) == 0x006647A8u - 0x0048E000u, "crt_fltout_raw");
static_assert(offsetof(struct AM2_OrigState, crt_strflt) == 0x006647C8u - 0x0048E000u, "crt_strflt");
static_assert(offsetof(struct AM2_OrigState, crt_commode) == 0x006647D8u - 0x0048E000u, "crt_commode");
static_assert(offsetof(struct AM2_OrigState, crt_seh_old_filter) == 0x006647DCu - 0x0048E000u, "crt_seh_old_filter");
static_assert(offsetof(struct AM2_OrigState, crt_tz_api_used) == 0x006647E0u - 0x0048E000u, "crt_tz_api_used");
static_assert(offsetof(struct AM2_OrigState, crt_tz_info) == 0x006647E8u - 0x0048E000u, "crt_tz_info");
static_assert(offsetof(struct AM2_OrigState, crt_last_tz) == 0x00664894u - 0x0048E000u, "crt_last_tz");
static_assert(offsetof(struct AM2_OrigState, crt_tzset_done) == 0x00664898u - 0x0048E000u, "crt_tzset_done");
static_assert(offsetof(struct AM2_OrigState, crt_mbcp_from_system) == 0x006648A0u - 0x0048E000u, "crt_mbcp_from_system");
static_assert(offsetof(struct AM2_OrigState, crt_msgbox_fn) == 0x006648A4u - 0x0048E000u, "crt_msgbox_fn");
static_assert(offsetof(struct AM2_OrigState, crt_getactivewindow_fn) == 0x006648A8u - 0x0048E000u, "crt_getactivewindow_fn");
static_assert(offsetof(struct AM2_OrigState, crt_getlastactivepopup_fn) == 0x006648ACu - 0x0048E000u, "crt_getlastactivepopup_fn");
static_assert(offsetof(struct AM2_OrigState, crt_fmode) == 0x006648B0u - 0x0048E000u, "crt_fmode");
static_assert(offsetof(struct AM2_OrigState, crt_mbcodepage) == 0x006648CCu - 0x0048E000u, "crt_mbcodepage");
static_assert(offsetof(struct AM2_OrigState, crt_mbulinfo) == 0x006648D0u - 0x0048E000u, "crt_mbulinfo");
static_assert(offsetof(struct AM2_OrigState, crt_ismbcodepage) == 0x006648DCu - 0x0048E000u, "crt_ismbcodepage");
static_assert(offsetof(struct AM2_OrigState, crt_mbcasemap) == 0x006648E0u - 0x0048E000u, "crt_mbcasemap");
static_assert(offsetof(struct AM2_OrigState, crt_mbctype) == 0x006649E0u - 0x0048E000u, "crt_mbctype");
static_assert(offsetof(struct AM2_OrigState, crt_mblcid) == 0x00664AE4u - 0x0048E000u, "crt_mblcid");
static_assert(offsetof(struct AM2_OrigState, crt_piob) == 0x00664AE8u - 0x0048E000u, "crt_piob");
static_assert(offsetof(struct AM2_OrigState, crt_nstream) == 0x00665B00u - 0x0048E000u, "crt_nstream");
static_assert(offsetof(struct AM2_OrigState, crt_pioinfo) == 0x00665B20u - 0x0048E000u, "crt_pioinfo");
static_assert(offsetof(struct AM2_OrigState, crt_nhandle) == 0x00665C20u - 0x0048E000u, "crt_nhandle");
static_assert(offsetof(struct AM2_OrigState, crt_env_initialized) == 0x00665C24u - 0x0048E000u, "crt_env_initialized");
static_assert(offsetof(struct AM2_OrigState, crt_mbctable_init) == 0x00665C28u - 0x0048E000u, "crt_mbctable_init");
static_assert(offsetof(struct AM2_OrigState, crt_onexitend) == 0x00665C2Cu - 0x0048E000u, "crt_onexitend");
static_assert(offsetof(struct AM2_OrigState, crt_onexitbegin) == 0x00665C30u - 0x0048E000u, "crt_onexitbegin");
static_assert(offsetof(struct AM2_OrigState, crt_sbh_size_header_list) == 0x00665C34u - 0x0048E000u, "crt_sbh_size_header_list");
static_assert(offsetof(struct AM2_OrigState, crt_sbh_ind_group_defer) == 0x00665C38u - 0x0048E000u, "crt_sbh_ind_group_defer");
static_assert(offsetof(struct AM2_OrigState, crt_sbh_pheader_scan) == 0x00665C3Cu - 0x0048E000u, "crt_sbh_pheader_scan");
static_assert(offsetof(struct AM2_OrigState, crt_sbh_pheader_defer) == 0x00665C40u - 0x0048E000u, "crt_sbh_pheader_defer");
static_assert(offsetof(struct AM2_OrigState, crt_sbh_cnt_header_list) == 0x00665C44u - 0x0048E000u, "crt_sbh_cnt_header_list");
static_assert(offsetof(struct AM2_OrigState, crt_sbh_pheader_list) == 0x00665C48u - 0x0048E000u, "crt_sbh_pheader_list");
static_assert(offsetof(struct AM2_OrigState, crt_crtheap) == 0x00665C4Cu - 0x0048E000u, "crt_crtheap");
static_assert(offsetof(struct AM2_OrigState, crt_acmdln) == 0x00665C50u - 0x0048E000u, "crt_acmdln");
static_assert(sizeof(struct AM2_OrigState) == 0x00666000u - 0x0048E000u, "AM2_OrigState size");

extern "C" struct AM2_OrigState am2_origstate __attribute__((section(".origbss")));
struct AM2_OrigState am2_origstate __attribute__((section(".origbss")));
#endif /* AM2_STANDALONE */
