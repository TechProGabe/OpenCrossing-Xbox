# Platform API — PC-only features (settings, profiler, overlay, texture packs)

Symbols whose disposition is `drop` or `rewrite/trim` because they exist for the
desktop port: settings, profiler, keybindings, typing, overlay, model viewer,
HD texture packs.
Read only when deciding what *not* to port.
Split out of `kb/design-platform-api.md` §5. Legend and dispositions: `kb/platform-api-overview.md`. Index: `kb/design-platform-api.md`.

### Settings

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_settings_cull_limit_xz` | `float pc_settings_cull_limit_xz(float cull_distance, float cull_radius)` | pc_settings.c | rewrite-for-KOS |  |
| `pc_settings_save` | `void pc_settings_save(void)` | pc_settings.c | rewrite-for-KOS |  |
| `pc_settings_get_nes_aspect` ✱ | `int pc_settings_get_nes_aspect(void)` | pc_settings.c | rewrite-for-KOS |  |
| `pc_settings_reset_controllers` ✱ | `void pc_settings_reset_controllers(void)` | pc_settings.c | rewrite-for-KOS |  |
| `pc_settings_autodetect_resolution` ✱ | `void pc_settings_autodetect_resolution(void)` | pc_settings.c | rewrite-for-KOS |  |
| `pc_settings_apply` | `void pc_settings_apply(void)` | pc_settings.c | rewrite-for-KOS |  |
| `pc_settings_load` | `void pc_settings_load(void)` | pc_settings.c | rewrite-for-KOS |  |

### Profiler

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_prof_now_us` ✱ | `unsigned long long pc_prof_now_us(void)` | pc_prof.c | rewrite-for-KOS |  |
| `pc_prof_report` ✱ | `void pc_prof_report(const char* tag, int id, unsigned long long t0_us)` | pc_prof.c | rewrite-for-KOS |  |

### PC-only features

| symbol | signature | pc/ file | DC disposition | notes |
|---|---|---|---|---|
| `pc_keybindings_save` ✱ | `void pc_keybindings_save(void)` | pc_keybindings.c | drop |  |
| `pc_keybindings_reset` ✱ | `void pc_keybindings_reset(void)` | pc_keybindings.c | drop |  |
| `pc_keybindings_uses_gamepad` ✱ | `int pc_keybindings_uses_gamepad(void)` | pc_keybindings.c | drop |  |
| `pc_keybinding_label` ✱ | `const char* pc_keybinding_label(int idx)` | pc_keybindings.c | drop |  |
| `pc_keybinding_ptr` ✱ | `PCInputCode* pc_keybinding_ptr(int idx)` | pc_keybindings.c | drop |  |
| `pc_keybindings_load` ✱ | `void pc_keybindings_load(void)` | pc_keybindings.c | drop |  |
| `pc_model_viewer_init` | `void pc_model_viewer_init(GAME* game)` | pc_model_viewer.c | drop |  |
| `pc_model_viewer_cleanup` ✱ | `void pc_model_viewer_cleanup(GAME* game)` | pc_model_viewer.c | drop |  |
| `pc_overlay_init` ✱ | `void pc_overlay_init(void)` | pc_overlay.c | drop |  |
| `pc_overlay_shutdown` ✱ | `void pc_overlay_shutdown(void)` | pc_overlay.c | drop |  |
| `pc_overlay_update` ✱ | `void pc_overlay_update(double fps, double speed)` | pc_overlay.c | drop |  |
| `pc_overlay_menu_toggle` ✱ | `void pc_overlay_menu_toggle(void)` | pc_overlay.c | drop |  |
| `pc_overlay_boot_splash` ✱ | `void pc_overlay_boot_splash(const char* msg)` | pc_overlay.c | drop |  |
| `pc_overlay_boot_error_frame` ✱ | `void pc_overlay_boot_error_frame(const char* const* lines, int n_lines)` | pc_overlay.c | drop |  |
| `pc_overlay_draw` ✱ | `void pc_overlay_draw(void)` | pc_overlay.c | drop |  |
| `pc_texture_pack_init` ✱ | `void pc_texture_pack_init(void)` | pc_texture_pack.c | drop |  |
| `pc_texture_pack_preload_all` ✱ | `void pc_texture_pack_preload_all(void)` | pc_texture_pack.c | drop |  |
| `pc_texture_pack_shutdown` ✱ | `void pc_texture_pack_shutdown(void)` | pc_texture_pack.c | drop |  |
| `pc_texture_pack_active` ✱ | `int pc_texture_pack_active(void)` | pc_texture_pack.c | drop |  |
| `pc_texture_pack_lookup` ✱ | `GLuint pc_texture_pack_lookup(const void* data, int data_size, int w, int h, unsigned int fmt, const void* tlut_data, int tlut_entries, int tlut_is_be, int* out_w, int* out_h)` | pc_texture_pack.c | drop |  |
| `pc_typing_queue_clear` ✱ | `void pc_typing_queue_clear(void)` | pc_typing.c | drop |  |
| `pc_typing_queue_push` ✱ | `void pc_typing_queue_push(int code)` | pc_typing.c | drop |  |
| `pc_typing_queue_pop` | `int pc_typing_queue_pop(int* out)` | pc_typing.c | drop |  |
| `pc_utf8_to_game_code` ✱ | `int pc_utf8_to_game_code(const char* text)` | pc_typing.c | drop |  |
| `pc_typing_handle_event` ✱ | `void pc_typing_handle_event(const SDL_Event* event)` | pc_typing.c | drop |  |
| `pc_typing_update` ✱ | `void pc_typing_update(void)` | pc_typing.c | drop |  |
