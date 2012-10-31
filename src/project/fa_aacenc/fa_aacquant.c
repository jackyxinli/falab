#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "fa_aaccfg.h"
#include "fa_aacquant.h"
#include "fa_aacpsy.h"
#include "fa_swbtab.h"
#include "fa_mdctquant.h"
#include "fa_aacstream.h"
#include "fa_timeprofile.h"


#ifndef FA_MIN
#define FA_MIN(a,b)  ( (a) < (b) ? (a) : (b) )
#endif 

#ifndef FA_MAX
#define FA_MAX(a,b)  ( (a) > (b) ? (a) : (b) )
#endif

#ifndef FA_ABS 
#define FA_ABS(a)    ( (a) > 0 ? (a) : (-(a)) )
#endif


static void calculate_start_common_scalefac(fa_aacenc_ctx_t *f)
{
    int i, chn;
    int chn_num;
    aacenc_ctx_t *s, *sl, *sr;

    chn_num = f->cfg.chn_num;

    i = 0;
    chn = 1;
    while (i < chn_num) {
        s = &(f->ctx[i]);
        /*s->chn_info.common_window = 0;*/

        if (s->chn_info.cpe == 1) {
            chn = 2;
            sl = s;
            sr = &(f->ctx[i+1]);
            if (sl->chn_info.common_window == 1) {
                float max_mdct_line;
                max_mdct_line = FA_MAX(sl->max_mdct_line, sr->max_mdct_line);
                sl->start_common_scalefac = fa_get_start_common_scalefac(max_mdct_line);
                sr->start_common_scalefac = sl->start_common_scalefac;
            } else {
                sl->start_common_scalefac = fa_get_start_common_scalefac(sl->max_mdct_line);
                sr->start_common_scalefac = fa_get_start_common_scalefac(sr->max_mdct_line);
            }
        } else if (s->chn_info.sce == 1) {
            chn = 1;
            s->start_common_scalefac = fa_get_start_common_scalefac(s->max_mdct_line);
        } else  { //lfe
            chn = 1;
            s->start_common_scalefac = fa_get_start_common_scalefac(s->max_mdct_line);
        }

        i += chn;
    }

}

static void init_quant_change(int outer_loop_count, aacenc_ctx_t *s)
{
    if (outer_loop_count == 0) {
        s->common_scalefac = s->start_common_scalefac;
        s->quant_change = 64;
    } else {
        s->quant_change = 2;
    }

}

static void init_quant_change_fast(int outer_loop_count, aacenc_ctx_t *s)
{
    if (outer_loop_count == 0) {
        s->common_scalefac = FA_MAX(s->start_common_scalefac, s->last_common_scalefac);
        /*s->common_scalefac = s->start_common_scalefac;*/
        s->quant_change = 64;
    } else {
        /*s->common_scalefac = FA_MAX(s->start_common_scalefac, s->last_common_scalefac);*/
        s->quant_change = 2;
    }

}


static void quant_innerloop(fa_aacenc_ctx_t *f, int outer_loop_count)
{
    int i, chn;
    int chn_num;
    aacenc_ctx_t *s, *sl, *sr;
    int counted_bits;
    int available_bits;
    int available_bits_l, available_bits_r;
    int find_globalgain;
    int head_end_sideinfo_avg;
    int inner_loop_cnt = 0;

    chn_num = f->cfg.chn_num;
    head_end_sideinfo_avg = fa_bits_sideinfo_est(chn_num);

    i = 0;
    chn = 1;
    while (i < chn_num) {
        inner_loop_cnt = 0;
        s = &(f->ctx[i]);

        if (s->chn_info.cpe == 1) {
            chn = 2;
            sl = s;
            sr = &(f->ctx[i+1]);
            find_globalgain = 0;
            init_quant_change(outer_loop_count, sl);
            init_quant_change(outer_loop_count, sr);
        } else {
            chn = 1;
            find_globalgain = 0;
            init_quant_change(outer_loop_count, s);
        }

        do {
            if (s->chn_info.cpe == 1) {
                if (sl->chn_info.common_window == 1) {
                    if (sl->quant_ok && sr->quant_ok) 
                        break;

                    if (s->block_type == ONLY_SHORT_BLOCK) {
                        fa_mdctline_quant(sl->h_mdctq_short, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_short, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                        fa_mdctline_quant(sr->h_mdctq_short, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_short, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    } else {
                        fa_mdctline_quant(sl->h_mdctq_long, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_long, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                        fa_mdctline_quant(sr->h_mdctq_long, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_long, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    }
                    
                    sr->max_sfb = FA_MAX(sr->max_sfb, sl->max_sfb);
                    sl->max_sfb = sr->max_sfb;

                } else {
                    if (sl->quant_ok && sr->quant_ok)
                        break;

                    if (sl->block_type == ONLY_SHORT_BLOCK) {
                        fa_mdctline_quant(sl->h_mdctq_short, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_short, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                    } else {
                        fa_mdctline_quant(sl->h_mdctq_long, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_long, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                    }

                    if (sr->block_type == ONLY_SHORT_BLOCK) {
                        fa_mdctline_quant(sr->h_mdctq_short, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_short, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    } else {
                        fa_mdctline_quant(sr->h_mdctq_long, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_long, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    }

                }

                counted_bits  = fa_bits_count(f->h_bitstream, &f->cfg, sl, sr) + head_end_sideinfo_avg*2;
                available_bits_l = get_avaiable_bits(sl->bits_average, sl->bits_more, sl->bits_res_size, sl->bits_res_maxsize);
                available_bits_r = get_avaiable_bits(sr->bits_average, sr->bits_more, sr->bits_res_size, sr->bits_res_maxsize);
                available_bits = available_bits_l + available_bits_r;

                if (counted_bits > available_bits) { 
                    sl->common_scalefac += sl->quant_change;
                    sr->common_scalefac += sr->quant_change;
                    sl->common_scalefac = FA_MIN(sl->common_scalefac, 255);
                    sr->common_scalefac = FA_MIN(sr->common_scalefac, 255);
                } else {
                    if (sl->quant_change > 1) {
                        sl->common_scalefac -= sl->quant_change;
                        sl->common_scalefac = FA_MAX(sl->common_scalefac, 0);
                    }
                    if (sr->quant_change > 1) {
                        sr->common_scalefac -= sr->quant_change;
                        sr->common_scalefac = FA_MAX(sr->common_scalefac, 0);
                    }
                }

                sl->quant_change >>= 1;
                sr->quant_change >>= 1;

                if (sl->quant_change == 0 && sr->quant_change == 0 &&
                   counted_bits>available_bits) {
                   sl->quant_change = 1;
                   sr->quant_change = 1;
                }

                if (sl->quant_change == 0 && sr->quant_change == 0)
                    find_globalgain = 1;
                else 
                    find_globalgain = 0;

            } else if (s->chn_info.sce == 1) {
                chn = 1;
                if (s->quant_ok)
                    break;
                if (s->block_type == ONLY_SHORT_BLOCK) {
                    fa_mdctline_quant(s->h_mdctq_short, s->common_scalefac, s->x_quant);
                    s->spectral_count = fa_mdctline_encode(s->h_mdctq_short, s->x_quant, s->num_window_groups, s->window_group_length, 
                                                           s->hufftab_no, &(s->max_sfb), s->x_quant_code, s->x_quant_bits);
                } else {
                    fa_mdctline_quant(s->h_mdctq_long, s->common_scalefac, s->x_quant);
                    s->spectral_count = fa_mdctline_encode(s->h_mdctq_long, s->x_quant, s->num_window_groups, s->window_group_length, 
                                                           s->hufftab_no, &(s->max_sfb), s->x_quant_code, s->x_quant_bits);
                }


                counted_bits  = fa_bits_count(f->h_bitstream, &f->cfg, s, NULL) + head_end_sideinfo_avg;

                available_bits = get_avaiable_bits(s->bits_average, s->bits_more, s->bits_res_size, s->bits_res_maxsize);
                if (counted_bits > available_bits) {
                    s->common_scalefac += s->quant_change;
                    s->common_scalefac = FA_MIN(s->common_scalefac, 255);
                }
                else {
                    if (s->quant_change > 1) {
                        s->common_scalefac -= s->quant_change;
                        s->common_scalefac = FA_MAX(s->common_scalefac, 0);
                    }
                }

                s->quant_change >>= 1;

                if (s->quant_change == 0 && 
                   counted_bits>available_bits)
                    s->quant_change = 1;
 
                if (s->quant_change == 0)
                    find_globalgain = 1;
                else 
                    find_globalgain = 0;

            } else {
                ; // lfe left
            }

            inner_loop_cnt++;
        } while (find_globalgain == 0);
        /*printf("the %d chn inner loop cnt=%d\n", i, inner_loop_cnt);*/
     
        if (s->chn_info.cpe == 1) {
            sl = s;
            sr = &(f->ctx[i+1]);
            sl->used_bits = counted_bits>>1;
            sl->bits_res_size = available_bits_l - sl->used_bits;
            if (sl->bits_res_size < 0)
                sl->bits_res_size = 0;
            if (sl->bits_res_size >= sl->bits_res_maxsize)
                sl->bits_res_size = sl->bits_res_maxsize;
            sr->used_bits = counted_bits - sl->used_bits;
            sr->bits_res_size = available_bits_r - sr->used_bits;
            if (sr->bits_res_size < 0)
                sr->bits_res_size = 0;
            if (sr->bits_res_size >= sr->bits_res_maxsize)
                sr->bits_res_size = sr->bits_res_maxsize;
        } else {
            s->used_bits = counted_bits;
            s->bits_res_size = available_bits - counted_bits;
            if (s->bits_res_size >= s->bits_res_maxsize)
                s->bits_res_size = s->bits_res_maxsize;
        }

        i += chn;
    } 


}


static int choose_search_step(int delta_bits)
{
	int step;

	if (delta_bits < 128)
		step = 1;
	else if (delta_bits < 256)
		step = 2;
	else if (delta_bits < 512)
		step = 4;
	else if (delta_bits < 768)
		step = 16;
	else if (delta_bits < 1024)
		step = 32;
	else 
		step = 64;

	return step;

}

static void quant_innerloop_fast(fa_aacenc_ctx_t *f, int outer_loop_count)
{
    int i, chn;
    int chn_num;
    aacenc_ctx_t *s, *sl, *sr;
    int counted_bits;
    int available_bits;
    int available_bits_l, available_bits_r;
    int find_globalgain;
    int head_end_sideinfo_avg;
    int inner_loop_cnt = 0;

    int delta_bits;

    chn_num = f->cfg.chn_num;
    head_end_sideinfo_avg = fa_bits_sideinfo_est(chn_num);

    i = 0;
    chn = 1;
    while (i < chn_num) {
        inner_loop_cnt = 0;
        s = &(f->ctx[i]);

        if (s->chn_info.cpe == 1) {
            chn = 2;
            sl = s;
            sr = &(f->ctx[i+1]);
            find_globalgain = 0;
            init_quant_change_fast(outer_loop_count, sl);
            /*init_quant_change_fast(outer_loop_count, sr);*/
            sr->common_scalefac = sl->common_scalefac;
        } else {
            chn = 1;
            find_globalgain = 0;
            init_quant_change_fast(outer_loop_count, s);
        }

        do {
            if (s->chn_info.cpe == 1) {
                if (sl->chn_info.common_window == 1) {
                    if (sl->quant_ok && sr->quant_ok) 
                        break;

                    if (s->block_type == ONLY_SHORT_BLOCK) {
                        fa_mdctline_quant(sl->h_mdctq_short, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_short, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                        fa_mdctline_quant(sr->h_mdctq_short, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_short, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    } else {
                        fa_mdctline_quant(sl->h_mdctq_long, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_long, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                        fa_mdctline_quant(sr->h_mdctq_long, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_long, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    }
                    
                    sr->max_sfb = FA_MAX(sr->max_sfb, sl->max_sfb);
                    sl->max_sfb = sr->max_sfb;

                } else {
                    if (sl->quant_ok && sr->quant_ok)
                        break;

                    if (sl->block_type == ONLY_SHORT_BLOCK) {
                        fa_mdctline_quant(sl->h_mdctq_short, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_short, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                    } else {
                        fa_mdctline_quant(sl->h_mdctq_long, sl->common_scalefac, sl->x_quant);
                        sl->spectral_count = fa_mdctline_encode(sl->h_mdctq_long, sl->x_quant, sl->num_window_groups, sl->window_group_length, 
                                                                sl->hufftab_no, &(sl->max_sfb), sl->x_quant_code, sl->x_quant_bits);
                    }

                    if (sr->block_type == ONLY_SHORT_BLOCK) {
                        fa_mdctline_quant(sr->h_mdctq_short, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_short, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    } else {
                        fa_mdctline_quant(sr->h_mdctq_long, sr->common_scalefac, sr->x_quant);
                        sr->spectral_count = fa_mdctline_encode(sr->h_mdctq_long, sr->x_quant, sr->num_window_groups, sr->window_group_length, 
                                                                sr->hufftab_no, &(sr->max_sfb), sr->x_quant_code, sr->x_quant_bits);
                    }

                }

                counted_bits  = fa_bits_count(f->h_bitstream, &f->cfg, sl, sr) + head_end_sideinfo_avg*2;
                available_bits_l = get_avaiable_bits(sl->bits_average, sl->bits_more, sl->bits_res_size, sl->bits_res_maxsize);
                available_bits_r = get_avaiable_bits(sr->bits_average, sr->bits_more, sr->bits_res_size, sr->bits_res_maxsize);
                available_bits = available_bits_l + available_bits_r;

                delta_bits = counted_bits - available_bits;
                delta_bits = FA_ABS(delta_bits);

                if (inner_loop_cnt == 0) 
                    sl->quant_change = sr->quant_change = choose_search_step(delta_bits);

                if (counted_bits > available_bits) { 
                    sl->common_scalefac += sl->quant_change;
                    sr->common_scalefac += sr->quant_change;
                    sl->common_scalefac = FA_MIN(sl->common_scalefac, 255);
                    sr->common_scalefac = FA_MIN(sr->common_scalefac, 255);
                } else {
                    sl->common_scalefac -= sl->quant_change;
                    sl->common_scalefac = FA_MAX(sl->common_scalefac, sl->start_common_scalefac);
                    sr->common_scalefac -= sr->quant_change;
                    sr->common_scalefac = FA_MAX(sr->common_scalefac, sl->start_common_scalefac);
                }

                sl->quant_change >>= 1;
                sr->quant_change >>= 1;

                if ((sl->quant_change == 0 && sr->quant_change == 0) && 
                    (counted_bits > available_bits)) {
                   sl->quant_change = 1;
                   sr->quant_change = 1;
                }

                if ((sl->quant_change == 0 && sr->quant_change == 0) 
                    /*|| (delta_bits < 20)*/
                    /*|| (inner_loop_cnt >= 6 && delta_bits < 200)*/
                    /*|| (inner_loop_cnt >= 7)*/
                    )
                    find_globalgain = 1;
                else 
                    find_globalgain = 0;

            } else if (s->chn_info.sce == 1) {
                chn = 1;
                if (s->quant_ok)
                    break;
                if (s->block_type == ONLY_SHORT_BLOCK) {
                    fa_mdctline_quant(s->h_mdctq_short, s->common_scalefac, s->x_quant);
                    s->spectral_count = fa_mdctline_encode(s->h_mdctq_short, s->x_quant, s->num_window_groups, s->window_group_length, 
                                                           s->hufftab_no, &(s->max_sfb), s->x_quant_code, s->x_quant_bits);
                } else {
                    fa_mdctline_quant(s->h_mdctq_long, s->common_scalefac, s->x_quant);
                    s->spectral_count = fa_mdctline_encode(s->h_mdctq_long, s->x_quant, s->num_window_groups, s->window_group_length, 
                                                           s->hufftab_no, &(s->max_sfb), s->x_quant_code, s->x_quant_bits);
                }


                counted_bits  = fa_bits_count(f->h_bitstream, &f->cfg, s, NULL) + head_end_sideinfo_avg;

                available_bits = get_avaiable_bits(s->bits_average, s->bits_more, s->bits_res_size, s->bits_res_maxsize);

                delta_bits = counted_bits - available_bits;
                delta_bits = FA_ABS(delta_bits);

                if (inner_loop_cnt == 0) 
                    s->quant_change = choose_search_step(delta_bits);


                if (counted_bits > available_bits) {
                    s->common_scalefac += s->quant_change;
                    s->common_scalefac = FA_MIN(s->common_scalefac, 255);
                }
                else {
                    s->common_scalefac -= s->quant_change;
                    s->common_scalefac = FA_MAX(s->common_scalefac, s->start_common_scalefac);
                }

                s->quant_change >>= 1;

                if (s->quant_change == 0 && 
                   counted_bits>available_bits)
                    s->quant_change = 1;
 
                if (s->quant_change == 0)
                    find_globalgain = 1;
                else 
                    find_globalgain = 0;

            } else {
                ; // lfe left
            }

            inner_loop_cnt++;
        } while (find_globalgain == 0);
        /*printf("the %d chn inner loop fast cnt=%d\n", i, inner_loop_cnt);*/
     
        if (s->chn_info.cpe == 1) {
            sl = s;
            sr = &(f->ctx[i+1]);
            sl->used_bits = counted_bits>>1;
            sl->bits_res_size = available_bits_l - sl->used_bits;
            if (sl->bits_res_size < -sl->bits_res_maxsize)
                sl->bits_res_size = -sl->bits_res_maxsize;
            if (sl->bits_res_size >= sl->bits_res_maxsize)
                sl->bits_res_size = sl->bits_res_maxsize;
            sr->used_bits = counted_bits - sl->used_bits;
            sr->bits_res_size = available_bits_r - sr->used_bits;
            if (sr->bits_res_size < -sr->bits_res_maxsize)
                sr->bits_res_size = -sr->bits_res_maxsize;
            if (sr->bits_res_size >= sr->bits_res_maxsize)
                sr->bits_res_size = sr->bits_res_maxsize;

            sl->last_common_scalefac = sl->common_scalefac;
            sr->last_common_scalefac = sl->common_scalefac;
        } else {
            s->used_bits = counted_bits;
            s->bits_res_size = available_bits - counted_bits;
            if (s->bits_res_size >= s->bits_res_maxsize)
                s->bits_res_size = s->bits_res_maxsize;
            s->last_common_scalefac = s->common_scalefac;
        }

        i += chn;
    } 


}



static void quant_outerloop(fa_aacenc_ctx_t *f)
{
    int i, chn;
    int chn_num;
    aacenc_ctx_t *s, *sl, *sr;
    int quant_ok_cnt;
    int outer_loop_count;

    chn_num = f->cfg.chn_num;

    quant_ok_cnt = 0;
    outer_loop_count = 0;
    do {
        /*scale the mdctline firstly using scalefactor*/
        i = 0;
        chn = 1;
        while (i < chn_num) {
            s = &(f->ctx[i]);

            if (s->chn_info.cpe == 1) {
                chn = 2;
                sl = s;
                sr = &(f->ctx[i+1]);
                if (s->chn_info.common_window == 1) {
                    if (!sl->quant_ok || !sr->quant_ok) {
                        if (s->block_type == ONLY_SHORT_BLOCK) {
                            fa_mdctline_scaled(sl->h_mdctq_short, s->num_window_groups, s->scalefactor);
                            fa_mdctline_scaled(sr->h_mdctq_short, s->num_window_groups, s->scalefactor);
                        } else {
                            fa_mdctline_scaled(sl->h_mdctq_long, s->num_window_groups, s->scalefactor);
                            fa_mdctline_scaled(sr->h_mdctq_long, s->num_window_groups, s->scalefactor);
                        }
                    }
                } else {
                    if (!sl->quant_ok || !sr->quant_ok) {
                        if (sl->block_type == ONLY_SHORT_BLOCK)
                            fa_mdctline_scaled(sl->h_mdctq_short, sl->num_window_groups, sl->scalefactor);
                        else 
                            fa_mdctline_scaled(sl->h_mdctq_long, sl->num_window_groups, sl->scalefactor);
                        if (sr->block_type == ONLY_SHORT_BLOCK)
                            fa_mdctline_scaled(sr->h_mdctq_short, sr->num_window_groups, sr->scalefactor);
                        else 
                            fa_mdctline_scaled(sr->h_mdctq_long, sr->num_window_groups, sr->scalefactor);
                    }
                }
            } else if (s->chn_info.sce == 1) {
                chn = 1;
                if (!s->quant_ok) {
                    if (s->block_type == ONLY_SHORT_BLOCK) 
                        fa_mdctline_scaled(s->h_mdctq_short, s->num_window_groups, s->scalefactor);
                    else 
                        fa_mdctline_scaled(s->h_mdctq_long, s->num_window_groups, s->scalefactor);
                }
            } else {
                chn = 1;
            }

            i += chn;
        }

        /*inner loop, search common_scalefac to fit the available_bits*/

        FA_CLOCK_START(3);
        /*quant_innerloop(f, outer_loop_count);*/
        quant_innerloop_fast(f, outer_loop_count);
        FA_CLOCK_END(3);
        FA_CLOCK_COST(3);


        /*calculate quant noise */
        for (i = 0; i < chn_num; i++) {
            s = &(f->ctx[i]);
            if (!s->quant_ok) {
                if (s->block_type == ONLY_SHORT_BLOCK) {
                    fa_calculate_quant_noise(s->h_mdctq_short,
                                             s->num_window_groups, s->window_group_length,
                                             s->common_scalefac, s->scalefactor, 
                                             s->x_quant);
                } else {
                    fa_calculate_quant_noise(s->h_mdctq_long,
                                             s->num_window_groups, s->window_group_length,
                                             s->common_scalefac, s->scalefactor, 
                                             s->x_quant);
                }
            }
        }

        /*search scalefactor to fix quant noise*/
        i = 0;
        chn = 1;
        quant_ok_cnt = 0;
        while (i < chn_num) {
            s = &(f->ctx[i]);

            if (s->chn_info.cpe == 1) {
                chn = 2;
                sl = s;
                sr = &(f->ctx[i+1]);
                if (s->chn_info.common_window == 1) {
                    if (!sl->quant_ok || !sr->quant_ok) {
                        if (s->block_type == ONLY_SHORT_BLOCK) {
                            sl->quant_ok = fa_fix_quant_noise_couple(sl->h_mdctq_short, sr->h_mdctq_short, outer_loop_count,
                                                                     s->num_window_groups, s->window_group_length,
                                                                     sl->scalefactor, sr->scalefactor, 
                                                                     s->x_quant);
                            sr->quant_ok = sl->quant_ok;
                            quant_ok_cnt += sl->quant_ok * 2;
                        } else {
                            sl->quant_ok = fa_fix_quant_noise_couple(sl->h_mdctq_long, sr->h_mdctq_long, outer_loop_count,
                                                                     s->num_window_groups, s->window_group_length,
                                                                     sl->scalefactor, sr->scalefactor,
                                                                     s->x_quant);
                            sr->quant_ok = sl->quant_ok;
                            quant_ok_cnt += sl->quant_ok * 2;
                        }
                    } else {
                        quant_ok_cnt += 2;
                    }
                } else {
                    if (!sl->quant_ok || !sr->quant_ok) {
                        if (sl->block_type == ONLY_SHORT_BLOCK) {
                            sl->quant_ok = fa_fix_quant_noise_single(sl->h_mdctq_short, outer_loop_count,
                                                                     sl->num_window_groups, sl->window_group_length,
                                                                     sl->scalefactor, 
                                                                     sl->x_quant);
                            quant_ok_cnt += sl->quant_ok;
                        } else {
                            sl->quant_ok = fa_fix_quant_noise_single(sl->h_mdctq_long, outer_loop_count,
                                                                     sl->num_window_groups, sl->window_group_length,
                                                                     sl->scalefactor, 
                                                                     sl->x_quant);
                            quant_ok_cnt += sl->quant_ok;
                        }
                        if (sr->block_type == ONLY_SHORT_BLOCK) {
                            sr->quant_ok = fa_fix_quant_noise_single(sr->h_mdctq_short, outer_loop_count,
                                                                     sr->num_window_groups, sr->window_group_length,
                                                                     sr->scalefactor, 
                                                                     sr->x_quant);
                            quant_ok_cnt += sr->quant_ok;
                        } else {
                            sr->quant_ok = fa_fix_quant_noise_single(sr->h_mdctq_long, outer_loop_count,
                                                                     sr->num_window_groups, sr->window_group_length,
                                                                     sr->scalefactor, 
                                                                     sr->x_quant);
                            quant_ok_cnt += sr->quant_ok;
                        }
                    } else {
                        quant_ok_cnt += 2;
                    }
                }
            } else if (s->chn_info.sce == 1) {
                chn = 1;
                if (!s->quant_ok) {
                    if (s->block_type == ONLY_SHORT_BLOCK) {
                        s->quant_ok = fa_fix_quant_noise_single(s->h_mdctq_short, outer_loop_count,
                                                                s->num_window_groups, s->window_group_length,
                                                                s->scalefactor, 
                                                                s->x_quant);
                        quant_ok_cnt += sr->quant_ok;
                    } else {
                        s->quant_ok = fa_fix_quant_noise_single(s->h_mdctq_long, outer_loop_count,
                                                                s->num_window_groups, s->window_group_length,
                                                                s->scalefactor, 
                                                                s->x_quant);
                        quant_ok_cnt += s->quant_ok;
                    }
                } else {
                    quant_ok_cnt += 1;
                }
            } else {
                chn = 1;
            }

            i += chn;
        }

        outer_loop_count++;

    } while (quant_ok_cnt < chn_num);

    /*printf("outer loop cnt= %d\n", outer_loop_count);*/
}


void fa_quantize_loop(fa_aacenc_ctx_t *f)
{
    int i;
    int chn_num;
    aacenc_ctx_t *s;

    chn_num = f->cfg.chn_num;

    /*FA_CLOCK_START(6);*/
    for (i = 0; i < chn_num; i++) {
        s = &(f->ctx[i]);
        if (s->block_type == ONLY_SHORT_BLOCK) {
            s->max_mdct_line = fa_mdctline_getmax(s->h_mdctq_short);
            fa_mdctline_pow34(s->h_mdctq_short);
            memset(s->scalefactor, 0, 8*FA_SWB_NUM_MAX*sizeof(int));
        } else {
            s->max_mdct_line = fa_mdctline_getmax(s->h_mdctq_long);
            fa_mdctline_pow34(s->h_mdctq_long);
            memset(s->scalefactor, 0, 8*FA_SWB_NUM_MAX*sizeof(int));
        }
    }
    /*FA_CLOCK_END(6);*/
    /*FA_CLOCK_COST(6);*/
     
    /*check common_window and start_common_scalefac*/
    calculate_start_common_scalefac(f);

    /*outer loop*/

    FA_CLOCK_START(2);
    quant_outerloop(f);
    FA_CLOCK_END(2);
    FA_CLOCK_COST(2);

}


/*this is the fast quantize, maybe is not very right, just for your test */

void  fa_calculate_sfb_avgenergy(aacenc_ctx_t *s)
{
    int   k;
    int   i;
    int   last = 0;
    float energy_sum = 0.0;
    float tmp;

    if (s->block_type == ONLY_SHORT_BLOCK) {
        for (k = 0; k < 8; k++) {
            last = 0;
            energy_sum = 0.0;
            for (i = 0; i < 128; i++) {
                tmp = s->mdct_line[k*128+i]; 
                if (tmp) {
                    last = i;
                    energy_sum += tmp * tmp;
                }
            }
            last++;
            s->lastx[k]     = last;
            f->avgenergy[k] = energy_sum / last;
        }
    } else {
        last = 0;
        energy_sum = 0.0;
        for (i = 0; i < 1024; i++) {
            tmp = mdct_line[i]; 
            if (tmp) {
                last = i;
                energy_sum += tmp * tmp;
            }
        }
        last++;
        s->lastx[0]     = last;
        s->avgenergy[0] = energy_sum / last;

    }

}


void fa_mdctline_calculate_xmin(aacenc_ctx_t *s, float xmin[8][NUM_SFB_MAX])
{
    float globalthr = 132./s->quality;

    fa_mdctquant_t *fs = (fa_mdctquant_t *)(s->h_mdctq_short);
    fa_mdctquant_t *fl = (fa_mdctquant_t *)(s->h_mdctq_long);

    int swb_num;
    int *swb_low;
    int *swb_high;

    int lastsb;
    float tmp;
    float energy;

    memset(xmin, 0, sizeof(float)*8*NUM_SFB_MAX);

    if (s->block_type == ONLY_SHORT_BLOCK) {
        swb_num  = fs->sfb_num;
        swb_low  = fs->swb_low;
        swb_high = fs->swb_high;

        for (k = 0; k < 8; k++) {
            lastsb = 0;
            for (i = 0; i < swb_num; i++) {
                if (s->lastx[k] > swb_low[i])
                    lastsb = i;
            }

            for (i = 0; i < swb_num; i++) {
                for (j = swb_low[i]; j <= swb_high[i]; j++) {
                }
            }
        }
    } else {
        sfb_num  = fl->sfb_num;
        swb_low  = fl->swb_low;
        swb_high = fl->swb_high;
         
        lastsb = 0;
        for (i = 0; i < swb_num; i++) {
            if (s->lastx[k] > swb_low[i])
                lastsb = i;
        }

        for (i = 0; i < swb_num; i++) {
            if (i > lastsb) {
                xmin[0][i] = 0;
                continue;
            }

            double enmax = -1.0;
            double lmax;

            lmax = start;
            for (l = start; l < end; l++)
            {
                if (enmax < (xr[l] * xr[l]))
                {
                    enmax = xr[l] * xr[l];
                    lmax = l;
                }
            }

            start = lmax - 2;
            end = lmax + 3;
            if (start < 0)
                start = 0;
            if (end > last)
                end = last;

            energy = 0.0;
            for (j = swb_low[i]; j <= swb_high[i]; j++) {
                tmp = s->mdct_line[j];
                energy += tmp * tmp;
            }

            thr = energy/(s->avgenergy[0] * (swb_high[i] - swb_low[i] + 1));
            thr = pow(thr, 0.1*(lastsb-i)/lastsb + 0.3);
            tmp = 1.0 - ((float)swb_low[i]/(float)swb_high[i]);
            tmp = tmp * tmp * tmp + 0.075;
            thr = 1.0 / (1.4*thr + tmp);

            xmin[i] = 1.12 * thr * globalthr;

        }
    }

}




static void CalcAllowedDist(CoderInfo *coderInfo, PsyInfo *psyInfo,
        double *xr, double *xmin, int quality)
{
    int sfb, start, end, l;
    const double globalthr = 132.0 / (double)quality;
    int last = coderInfo->lastx;
    int lastsb = 0;
    int *cb_offset = coderInfo->sfb_offset;
    int num_cb = coderInfo->nr_of_sfb;
    double avgenrg = coderInfo->avgenrg;

    for (sfb = 0; sfb < num_cb; sfb++)
    {
        if (last > cb_offset[sfb])
            lastsb = sfb;
    }

    for (sfb = 0; sfb < num_cb; sfb++)
    {
        double thr, tmp;
        double enrg = 0.0;

        start = cb_offset[sfb];
        end = cb_offset[sfb + 1];

        if (sfb > lastsb)
        {
            xmin[sfb] = 0;
            continue;
        }

        if (coderInfo->block_type != ONLY_SHORT_WINDOW)
        {
            double enmax = -1.0;
            double lmax;

            lmax = start;
            for (l = start; l < end; l++)
            {
                if (enmax < (xr[l] * xr[l]))
                {
                    enmax = xr[l] * xr[l];
                    lmax = l;
                }
            }

            start = lmax - 2;
            end = lmax + 3;
            if (start < 0)
                start = 0;
            if (end > last)
                end = last;
        }

        for (l = start; l < end; l++)
        {
            enrg += xr[l]*xr[l];
        }

        thr = enrg/((double)(end-start)*avgenrg);
        thr = pow(thr, 0.1*(lastsb-sfb)/lastsb + 0.3);

        tmp = 1.0 - ((double)start / (double)last);
        tmp = tmp * tmp * tmp + 0.075;

        thr = 1.0 / (1.4*thr + tmp);

        xmin[sfb] = ((coderInfo->block_type == ONLY_SHORT_WINDOW) ? 0.65 : 1.12)
            * globalthr * thr;
    }
}

void fa_psychomodel2_calculate_xmin(uintptr_t handle, float *mdct_line, float *xmin)
{
    int i,j;
    fa_psychomodel2_t *f = (fa_psychomodel2_t *)handle;

    float codec_e;
    int   swb_num    = f->swb_num;
    int   *swb_offset= f->swb_offset;
    float *smr       = f->smr;

    for (i = 0; i < swb_num; i++) {
        if (smr[i] > 0) {
            codec_e = 0;
            for (j = swb_offset[i]; j < swb_offset[i+1]; j++)
                codec_e = codec_e + mdct_line[j]*mdct_line[j];

            xmin[i] = codec_e/smr[i];
        } else {
            xmin[i] = 0;
        }
    }
}


static int calculate_xmin()
{


}


void fa_quantize_fast(fa_aacenc_ctx_t *f)
{


}
