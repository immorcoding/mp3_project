/* PROTOTYPE（#152，throwaway，不合入）：唱盘页歌手行版式，见 PROTOTYPE.md。 */
#ifndef PROTOTYPE_ARTIST_LAYOUT_H
#define PROTOTYPE_ARTIST_LAYOUT_H

#include <stdint.h>

void proto_artist_layout_init(int layout);
void proto_artist_layout_step(void);
void proto_artist_perf_window(const char *spec);
void proto_artist_perf_begin(void);
void proto_artist_perf_end(uint32_t script_ms);
void proto_artist_perf_report(void);

#endif /* PROTOTYPE_ARTIST_LAYOUT_H */
