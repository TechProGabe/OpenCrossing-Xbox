/* xbox_splash.h — boot title card + fatal error screen (xbox/src/xbox_splash.c). */
#ifndef XBOX_SPLASH_H
#define XBOX_SPLASH_H
#ifdef __cplusplus
extern "C" {
#endif
void xbox_splash_show(void);
void xbox_splash_progress(float f);
/* lines: NULL-terminated. Never returns. */
void xbox_splash_error(const char* title, const char* const* lines);
#ifdef __cplusplus
}
#endif
#endif
