#ifndef TUTOR_H
#define TUTOR_H

#include <stddef.h>

typedef struct {
  const char *label;
  const char *const *lines;
} TutorSection;

typedef struct {
  const char *title;
  const char *tagline;
  const char *title_color;
  size_t key_width;
  const TutorSection *sections;
  size_t section_count;
} TutorConfig;

extern const TutorConfig tutor_config;

#endif
