#ifndef OK_BOARD_LAYOUT_H
#define OK_BOARD_LAYOUT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double card_width, card_height;
    double top_width, top_height, top_inset;
    double column_gap, column_x[7], tableau_y;
    double down_spacing, up_spacing;
} OKBoardLayout;

// Counts describe visible overlap steps in each column, never card identities.
OKBoardLayout ok_board_layout(double width, double height, int landscape,
                              const int down[7], const int up[7]);
#ifdef __cplusplus
}
#endif
#endif
