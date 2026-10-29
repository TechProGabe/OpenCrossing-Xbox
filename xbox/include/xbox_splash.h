/* xbox_splash.h — boot title card + fatal error screen (xbox/src/xbox_splash.c). */
#ifndef XBOX_SPLASH_H
#define XBOX_SPLASH_H
#ifdef __cplusplus
extern "C" {
#endif
void xbox_splash_show(void);
void xbox_splash_progress(float f);
/* before anything else changes the video mode (xbox_nv2a.c, 720p) */
void xbox_splash_release(void);
/* the first boot's disc check (xbox_disc_check.c): the letter on (1) and,
 * once the check is done, the bar back under the stamps (0); the two
 * status lines (NULL keeps a line); stamp box i stamped (ok) or crossed out */
void xbox_splash_letter(int on);
void xbox_splash_text(const char* a, const char* b);
void xbox_splash_stamp(int i, int ok);
/* the bar to 100%, held a moment; call before the GPU takes the screen */
void xbox_splash_finish(void);
/* 1 while button b (SDL_GameControllerButton) is down on any pad */
int xbox_splash_button(int b);
/* Y held with both triggers as the splash ended: force the full disc check */
extern int g_xbox_force_disc_check;
/* a question card: 1 for A, 0 for B. lines: NULL-terminated */
int xbox_splash_ask(const char* title, const char* const* lines);
/* lines: NULL-terminated. Never returns. */
void xbox_splash_error(const char* title, const char* const* lines);
#ifdef __cplusplus
}
#endif
#endif
