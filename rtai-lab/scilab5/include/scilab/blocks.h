/*
* Scilab ( http://www.scilab.org/ ) - This file is part of Scilab
* Copyright (C) 2009 DIGITEO
* Copyright (C) 2009 Allan CORNET
*
* This file must be used under the terms of the CeCILL.
* This source file is licensed as described in the file COPYING, which
* you should have received as part of this distribution.  The terms
* are also available at
* http://www.cecill.info/licences/Licence_CeCILL_V2.1-en.txt
*
*/
#ifndef __SCICOS_BLOCKS__ 
#define __SCICOS_BLOCKS__ 
#include "scicos.h" 
/******* Copyright INRIA *************/
/******* Please do not edit (file automatically generated) *************/
extern void F2C(csslti) (ARGS_scicos);
extern void F2C(cstblk) (ARGS_scicos);
extern void F2C(delay) (ARGS_scicos);
extern void F2C(delayv) (ARGS_scicos);
extern void F2C(demux) (ARGS_scicos);
extern void F2C(diffblk) (ARGS_scicos);
extern void F2C(dlradp) (ARGS_scicos);
extern void F2C(dollar) (ARGS_scicos);
extern void F2C(dsslti) (ARGS_scicos);
extern void F2C(eselect) (ARGS_scicos);
extern void F2C(evtdly) (ARGS_scicos);
extern void F2C(expblk) (ARGS_scicos);
extern void F2C(forblk) (ARGS_scicos);
extern void F2C(fsv) (ARGS_scicos);
extern void F2C(gensin) (ARGS_scicos);
extern void F2C(gensqr) (ARGS_scicos);
extern void F2C(hltblk) (ARGS_scicos);
extern void F2C(ifthel) (ARGS_scicos);
extern void F2C(integr) (ARGS_scicos);
extern void F2C(intplt) (ARGS_scicos);
extern void F2C(intpol) (ARGS_scicos);
extern void F2C(intrp2) (ARGS_scicos);
extern void F2C(intrpl) (ARGS_scicos);
extern void F2C(invblk) (ARGS_scicos);
extern void F2C(iocopy) (ARGS_scicos);
extern void F2C(logblk) (ARGS_scicos);
extern void F2C(lookup) (ARGS_scicos);
extern void F2C(lsplit) (ARGS_scicos);
extern void F2C(lusat) (ARGS_scicos);
extern void F2C(maxblk) (ARGS_scicos);
extern void F2C(memo) (ARGS_scicos);
extern void F2C(mfclck) (ARGS_scicos);
extern void F2C(minblk) (ARGS_scicos);
extern void F2C(mux) (ARGS_scicos);
extern void F2C(pload) (ARGS_scicos);
extern void F2C(powblk) (ARGS_scicos);
extern void F2C(qzcel) (ARGS_scicos);
extern void F2C(qzflr) (ARGS_scicos);
extern void F2C(qzrnd) (ARGS_scicos);
extern void F2C(qztrn) (ARGS_scicos);
extern void F2C(readf) (ARGS_scicos);
extern void F2C(rndblk) (ARGS_scicos);
extern void F2C(samphold) (ARGS_scicos);
extern void F2C(sawtth) (ARGS_scicos);
extern void F2C(sciblk) (ARGS_scicos);
extern void F2C(selblk) (ARGS_scicos);
extern void F2C(sinblk) (ARGS_scicos);
extern void F2C(sqrblk) (ARGS_scicos);
extern void F2C(sum2) (ARGS_scicos);
extern void F2C(sum3) (ARGS_scicos);
extern void F2C(tanblk) (ARGS_scicos);
extern void F2C(tcslti) (ARGS_scicos);
extern void F2C(tcsltj) (ARGS_scicos);
extern void F2C(timblk) (ARGS_scicos);
extern void F2C(trash) (ARGS_scicos);
extern void F2C(writef) (ARGS_scicos);
extern void F2C(zcross) (ARGS_scicos);
extern void absblk (ARGS_scicos);
extern void absolute_value (ARGS_scicos);
extern void acos_blk (ARGS_scicos);
extern void acosh_blk (ARGS_scicos);
extern void andlog (ARGS_scicos);
extern void asin_blk (ARGS_scicos);
extern void asinh_blk (ARGS_scicos);
extern void assignment (ARGS_scicos);
extern void atan_blk (ARGS_scicos);
extern void atanh_blk (ARGS_scicos);
extern void automat (ARGS_scicos);
extern void backlash (ARGS_scicos);
extern void bidon (ARGS_scicos);
extern void bit_clear_16 (ARGS_scicos);
extern void bit_clear_32 (ARGS_scicos);
extern void bit_clear_8 (ARGS_scicos);
extern void bit_set_16 (ARGS_scicos);
extern void bit_set_32 (ARGS_scicos);
extern void bit_set_8 (ARGS_scicos);
extern void bounce_ball (ARGS_scicos);
extern void bouncexy (ARGS_scicos);
extern void canimxy3d (ARGS_scicos);
extern void canimxy (ARGS_scicos);
extern void cdummy (ARGS_scicos);
extern void cevscpe (ARGS_scicos);
extern void cfscope (ARGS_scicos);
extern void cmat3d (ARGS_scicos);
extern void cmatview (ARGS_scicos);
extern void cmscope (ARGS_scicos);
extern void constraint_c (ARGS_scicos);
extern void convert (ARGS_scicos);
extern void cos_blk (ARGS_scicos);
extern void cosblk (ARGS_scicos);
extern void cosh_blk (ARGS_scicos);
extern void counter (ARGS_scicos);
extern void cscope (ARGS_scicos);
extern void cscopxy3d (ARGS_scicos);
extern void cscopxy (ARGS_scicos);
extern void csslti4 (ARGS_scicos);
extern void cstblk4 (ARGS_scicos);
extern void cstblk4_m (ARGS_scicos);
extern void cumsum_c (ARGS_scicos);
extern void cumsum_m (ARGS_scicos);
extern void cumsum_r (ARGS_scicos);
extern void cumsumz_c (ARGS_scicos);
extern void cumsumz_m (ARGS_scicos);
extern void cumsumz_r (ARGS_scicos);
extern void curve_c (ARGS_scicos);
extern void dband (ARGS_scicos);
extern void deadband (ARGS_scicos);
extern void delay4 (ARGS_scicos);
extern void delay4_i16 (ARGS_scicos);
extern void delay4_i32 (ARGS_scicos);
extern void delay4_i8 (ARGS_scicos);
extern void delay4_ui16 (ARGS_scicos);
extern void delay4_ui32 (ARGS_scicos);
extern void delay4_ui8 (ARGS_scicos);
extern void deriv (ARGS_scicos);
extern void diffblk_c (ARGS_scicos);
extern void dmmul1 (ARGS_scicos);
extern void dmmul (ARGS_scicos);
extern void dollar4 (ARGS_scicos);
extern void dollar4_m (ARGS_scicos);
extern void dsslti4 (ARGS_scicos);
extern void edgetrig (ARGS_scicos);
extern void evaluate_expr (ARGS_scicos);
extern void evtdly4 (ARGS_scicos);
extern void evtvardly (ARGS_scicos);
extern void expblk_m (ARGS_scicos);
extern void extdiag (ARGS_scicos);
extern void extdiagz (ARGS_scicos);
extern void extract_bit_16_LH (ARGS_scicos);
extern void extract_bit_16_LSB (ARGS_scicos);
extern void extract_bit_16_MSB0 (ARGS_scicos);
extern void extract_bit_16_MSB1 (ARGS_scicos);
extern void extract_bit_16_RB0 (ARGS_scicos);
extern void extract_bit_16_RB1 (ARGS_scicos);
extern void extract_bit_16_UH0 (ARGS_scicos);
extern void extract_bit_16_UH1 (ARGS_scicos);
extern void extract_bit_32_LH (ARGS_scicos);
extern void extract_bit_32_LSB (ARGS_scicos);
extern void extract_bit_32_MSB0 (ARGS_scicos);
extern void extract_bit_32_MSB1 (ARGS_scicos);
extern void extract_bit_32_RB0 (ARGS_scicos);
extern void extract_bit_32_RB1 (ARGS_scicos);
extern void extract_bit_32_UH0 (ARGS_scicos);
extern void extract_bit_32_UH1 (ARGS_scicos);
extern void extract_bit_8_LH (ARGS_scicos);
extern void extract_bit_8_LSB (ARGS_scicos);
extern void extract_bit_8_MSB0 (ARGS_scicos);
extern void extract_bit_8_MSB1 (ARGS_scicos);
extern void extract_bit_8_RB0 (ARGS_scicos);
extern void extract_bit_8_RB1 (ARGS_scicos);
extern void extract_bit_8_UH0 (ARGS_scicos);
extern void extract_bit_8_UH1 (ARGS_scicos);
extern void extract_bit_u16_MSB1 (ARGS_scicos);
extern void extract_bit_u16_RB1 (ARGS_scicos);
extern void extract_bit_u16_UH1 (ARGS_scicos);
extern void extract_bit_u32_MSB1 (ARGS_scicos);
extern void extract_bit_u32_RB1 (ARGS_scicos);
extern void extract_bit_u32_UH1 (ARGS_scicos);
extern void extract_bit_u8_MSB1 (ARGS_scicos);
extern void extract_bit_u8_RB1 (ARGS_scicos);
extern void extract_bit_u8_UH1 (ARGS_scicos);
extern void extract (ARGS_scicos);
extern void extractor (ARGS_scicos);
extern void extractz (ARGS_scicos);
extern void exttril (ARGS_scicos);
extern void exttrilz (ARGS_scicos);
extern void exttriu (ARGS_scicos);
extern void exttriuz (ARGS_scicos);
extern void foriterator (ARGS_scicos);
extern void fromws_c (ARGS_scicos);
extern void gainblk (ARGS_scicos);
extern void gainblk_i16e (ARGS_scicos);
extern void gainblk_i16n (ARGS_scicos);
extern void gainblk_i16s (ARGS_scicos);
extern void gainblk_i32e (ARGS_scicos);
extern void gainblk_i32n (ARGS_scicos);
extern void gainblk_i32s (ARGS_scicos);
extern void gainblk_i8e (ARGS_scicos);
extern void gainblk_i8n (ARGS_scicos);
extern void gainblk_i8s (ARGS_scicos);
extern void gainblk_ui16e (ARGS_scicos);
extern void gainblk_ui16n (ARGS_scicos);
extern void gainblk_ui16s (ARGS_scicos);
extern void gainblk_ui32e (ARGS_scicos);
extern void gainblk_ui32n (ARGS_scicos);
extern void gainblk_ui32s (ARGS_scicos);
extern void gainblk_ui8e (ARGS_scicos);
extern void gainblk_ui8n (ARGS_scicos);
extern void gainblk_ui8s (ARGS_scicos);
extern void gain (ARGS_scicos);
extern void hystheresis (ARGS_scicos);
extern void integral_func (ARGS_scicos);
extern void integralz_func (ARGS_scicos);
extern void invblk4 (ARGS_scicos);
extern void logicalop (ARGS_scicos);
extern void logicalop_i16 (ARGS_scicos);
extern void logicalop_i32 (ARGS_scicos);
extern void logicalop_i8 (ARGS_scicos);
extern void logicalop_m (ARGS_scicos);
extern void logicalop_ui16 (ARGS_scicos);
extern void logicalop_ui32 (ARGS_scicos);
extern void logicalop_ui8 (ARGS_scicos);
extern void logic (ARGS_scicos);
extern void lookup2d (ARGS_scicos);
extern void lookup_c (ARGS_scicos);
extern void mat_bksl (ARGS_scicos);
extern void matbyscal (ARGS_scicos);
extern void matbyscal_e (ARGS_scicos);
extern void matbyscal_s (ARGS_scicos);
extern void mat_cath (ARGS_scicos);
extern void mat_catv (ARGS_scicos);
extern void mat_det (ARGS_scicos);
extern void mat_diag (ARGS_scicos);
extern void mat_div (ARGS_scicos);
extern void mat_expm (ARGS_scicos);
extern void mathermit_m (ARGS_scicos);
extern void mat_inv (ARGS_scicos);
extern void mat_lu (ARGS_scicos);
extern void matmul2_e (ARGS_scicos);
extern void matmul2_m (ARGS_scicos);
extern void matmul2_s (ARGS_scicos);
extern void matmul_i16e (ARGS_scicos);
extern void matmul_i16n (ARGS_scicos);
extern void matmul_i16s (ARGS_scicos);
extern void matmul_i32e (ARGS_scicos);
extern void matmul_i32n (ARGS_scicos);
extern void matmul_i32s (ARGS_scicos);
extern void matmul_i8e (ARGS_scicos);
extern void matmul_i8n (ARGS_scicos);
extern void matmul_i8s (ARGS_scicos);
extern void matmul_m (ARGS_scicos);
extern void matmul_ui16e (ARGS_scicos);
extern void matmul_ui16n (ARGS_scicos);
extern void matmul_ui16s (ARGS_scicos);
extern void matmul_ui32e (ARGS_scicos);
extern void matmul_ui32n (ARGS_scicos);
extern void matmul_ui32s (ARGS_scicos);
extern void matmul_ui8e (ARGS_scicos);
extern void matmul_ui8n (ARGS_scicos);
extern void matmul_ui8s (ARGS_scicos);
extern void mat_pinv (ARGS_scicos);
extern void mat_reshape (ARGS_scicos);
extern void mat_sing (ARGS_scicos);
extern void mat_sqrt (ARGS_scicos);
extern void mat_sum (ARGS_scicos);
extern void mat_sumc (ARGS_scicos);
extern void mat_suml (ARGS_scicos);
extern void mat_svd (ARGS_scicos);
extern void mattran_m (ARGS_scicos);
extern void mat_vps (ARGS_scicos);
extern void mat_vpv (ARGS_scicos);
extern void matz_abs (ARGS_scicos);
extern void matz_absc (ARGS_scicos);
extern void matz_bksl (ARGS_scicos);
extern void matz_cath (ARGS_scicos);
extern void matz_catv (ARGS_scicos);
extern void matz_conj (ARGS_scicos);
extern void matz_det (ARGS_scicos);
extern void matz_diag (ARGS_scicos);
extern void matz_div (ARGS_scicos);
extern void matz_expm (ARGS_scicos);
extern void matz_inv (ARGS_scicos);
extern void matz_lu (ARGS_scicos);
extern void matzmul2_m (ARGS_scicos);
extern void matzmul_m (ARGS_scicos);
extern void matz_pinv (ARGS_scicos);
extern void matz_reim (ARGS_scicos);
extern void matz_reimc (ARGS_scicos);
extern void matz_reshape (ARGS_scicos);
extern void matz_sing (ARGS_scicos);
extern void matz_sqrt (ARGS_scicos);
extern void matz_sum (ARGS_scicos);
extern void matz_sumc (ARGS_scicos);
extern void matz_suml (ARGS_scicos);
extern void matz_svd (ARGS_scicos);
extern void matztran_m (ARGS_scicos);
extern void matz_vps (ARGS_scicos);
extern void matz_vpv (ARGS_scicos);
extern void m_frequ (ARGS_scicos);
extern void minmax (ARGS_scicos);
extern void modulo_count (ARGS_scicos);
extern void mswitch (ARGS_scicos);
extern void multiplex (ARGS_scicos);
extern void plusblk (ARGS_scicos);
extern void prod (ARGS_scicos);
extern void product (ARGS_scicos);
extern void ramp (ARGS_scicos);
extern void ratelimiter (ARGS_scicos);
extern void readau (ARGS_scicos);
extern void readc (ARGS_scicos);
extern void relational_op (ARGS_scicos);
extern void relationalop (ARGS_scicos);
extern void relational_op_i16 (ARGS_scicos);
extern void relational_op_i32 (ARGS_scicos);
extern void relational_op_i8 (ARGS_scicos);
extern void relational_op_ui16 (ARGS_scicos);
extern void relational_op_ui32 (ARGS_scicos);
extern void relational_op_ui8 (ARGS_scicos);
extern void relay (ARGS_scicos);
extern void ricc_m (ARGS_scicos);
extern void rndblk_m (ARGS_scicos);
extern void rndblkz_m (ARGS_scicos);
extern void root_coef (ARGS_scicos);
extern void rootz_coef (ARGS_scicos);
extern void samphold4 (ARGS_scicos);
extern void samphold4_m (ARGS_scicos);
extern void satur (ARGS_scicos);
extern void scalar2vector (ARGS_scicos);
extern void scicosexit (ARGS_scicos);
extern void selector (ARGS_scicos);
extern void selector_m (ARGS_scicos);
extern void shift_16_LA (ARGS_scicos);
extern void shift_16_LC (ARGS_scicos);
extern void shift_16_RA (ARGS_scicos);
extern void shift_16_RC (ARGS_scicos);
extern void shift_32_LA (ARGS_scicos);
extern void shift_32_LC (ARGS_scicos);
extern void shift_32_RA (ARGS_scicos);
extern void shift_32_RC (ARGS_scicos);
extern void shift_8_LA (ARGS_scicos);
extern void shift_8_LC (ARGS_scicos);
extern void shift_8_RA (ARGS_scicos);
extern void shift_8_RC (ARGS_scicos);
extern void shift_u16_RA (ARGS_scicos);
extern void shift_u32_RA (ARGS_scicos);
extern void shift_u8_RA (ARGS_scicos);
extern void signum (ARGS_scicos);
extern void sin_blk (ARGS_scicos);
extern void sinh_blk (ARGS_scicos);
extern void step_func (ARGS_scicos);
extern void submat (ARGS_scicos);
extern void submatz (ARGS_scicos);
extern void sum (ARGS_scicos);
extern void summation (ARGS_scicos);
extern void summation_i16e (ARGS_scicos);
extern void summation_i16n (ARGS_scicos);
extern void summation_i16s (ARGS_scicos);
extern void summation_i32e (ARGS_scicos);
extern void summation_i32n (ARGS_scicos);
extern void summation_i32s (ARGS_scicos);
extern void summation_i8e (ARGS_scicos);
extern void summation_i8n (ARGS_scicos);
extern void summation_i8s (ARGS_scicos);
extern void summation_ui16e (ARGS_scicos);
extern void summation_ui16n (ARGS_scicos);
extern void summation_ui16s (ARGS_scicos);
extern void summation_ui32e (ARGS_scicos);
extern void summation_ui32n (ARGS_scicos);
extern void summation_ui32s (ARGS_scicos);
extern void summation_ui8e (ARGS_scicos);
extern void summation_ui8n (ARGS_scicos);
extern void summation_ui8s (ARGS_scicos);
extern void summation_z (ARGS_scicos);
extern void switch2 (ARGS_scicos);
extern void switch2_m (ARGS_scicos);
extern void switchn (ARGS_scicos);
extern void tablex2d_c (ARGS_scicos);
extern void tan_blk (ARGS_scicos);
extern void tanh_blk (ARGS_scicos);
extern void tcslti4 (ARGS_scicos);
extern void tcsltj4 (ARGS_scicos);
extern void time_delay (ARGS_scicos);
extern void tows_c (ARGS_scicos);
extern void variable_delay (ARGS_scicos);
extern void whileiterator (ARGS_scicos);
extern void writeau (ARGS_scicos);
extern void writec (ARGS_scicos);
extern void zcross2 (ARGS_scicos);
extern void affich2 (ARGS_scicos);
 
OpTab tabsim[] ={
{"absblk",(ScicosF) absblk},
{"absolute_value",(ScicosF) absolute_value},
{"acos_blk",(ScicosF) acos_blk},
{"acosh_blk",(ScicosF) acosh_blk},
{"affich2",(ScicosF4) affich2},
{"andlog",(ScicosF) andlog},
{"asin_blk",(ScicosF) asin_blk},
{"asinh_blk",(ScicosF) asinh_blk},
{"assignment",(ScicosF) assignment},
{"atan_blk",(ScicosF) atan_blk},
{"atanh_blk",(ScicosF) atanh_blk},
{"automat",(ScicosF) automat},
{"backlash",(ScicosF) backlash},
{"bidon",(ScicosF) bidon},
{"bit_clear_16",(ScicosF) bit_clear_16},
{"bit_clear_32",(ScicosF) bit_clear_32},
{"bit_clear_8",(ScicosF) bit_clear_8},
{"bit_set_16",(ScicosF) bit_set_16},
{"bit_set_32",(ScicosF) bit_set_32},
{"bit_set_8",(ScicosF) bit_set_8},
{"bounce_ball",(ScicosF) bounce_ball},
{"bouncexy",(ScicosF) bouncexy},
{"canimxy3d",(ScicosF) canimxy3d},
{"canimxy",(ScicosF) canimxy},
{"cdummy",(ScicosF) cdummy},
{"cevscpe",(ScicosF) cevscpe},
{"cfscope",(ScicosF) cfscope},
{"cmat3d",(ScicosF) cmat3d},
{"cmatview",(ScicosF) cmatview},
{"cmscope",(ScicosF) cmscope},
{"constraint_c",(ScicosF) constraint_c},
{"convert",(ScicosF) convert},
{"cos_blk",(ScicosF) cos_blk},
{"cosblk",(ScicosF) cosblk},
{"cosh_blk",(ScicosF) cosh_blk},
{"counter",(ScicosF) counter},
{"cscope",(ScicosF) cscope},
{"cscopxy3d",(ScicosF) cscopxy3d},
{"cscopxy",(ScicosF) cscopxy},
{"csslti4",(ScicosF) csslti4},
{"csslti",(ScicosF) F2C(csslti)},
{"cstblk4_m",(ScicosF) cstblk4_m},
{"cstblk4",(ScicosF) cstblk4},
{"cstblk",(ScicosF) F2C(cstblk)},
{"cumsum_c",(ScicosF) cumsum_c},
{"cumsum_m",(ScicosF) cumsum_m},
{"cumsum_r",(ScicosF) cumsum_r},
{"cumsumz_c",(ScicosF) cumsumz_c},
{"cumsumz_m",(ScicosF) cumsumz_m},
{"cumsumz_r",(ScicosF) cumsumz_r},
{"curve_c",(ScicosF) curve_c},
{"dband",(ScicosF) dband},
{"deadband",(ScicosF) deadband},
{"delay4_i16",(ScicosF) delay4_i16},
{"delay4_i32",(ScicosF) delay4_i32},
{"delay4_i8",(ScicosF) delay4_i8},
{"delay4",(ScicosF) delay4},
{"delay4_ui16",(ScicosF) delay4_ui16},
{"delay4_ui32",(ScicosF) delay4_ui32},
{"delay4_ui8",(ScicosF) delay4_ui8},
{"delay",(ScicosF) F2C(delay)},
{"delayv",(ScicosF) F2C(delayv)},
{"demux",(ScicosF) F2C(demux)},
{"deriv",(ScicosF) deriv},
{"diffblk_c",(ScicosF) diffblk_c},
{"diffblk",(ScicosF) F2C(diffblk)},
{"dlradp",(ScicosF) F2C(dlradp)},
{"dmmul1",(ScicosF) dmmul1},
{"dmmul",(ScicosF) dmmul},
{"dollar4_m",(ScicosF) dollar4_m},
{"dollar4",(ScicosF) dollar4},
{"dollar",(ScicosF) F2C(dollar)},
{"dsslti4",(ScicosF) dsslti4},
{"dsslti",(ScicosF) F2C(dsslti)},
{"edgetrig",(ScicosF) edgetrig},
{"eselect",(ScicosF) F2C(eselect)},
{"evaluate_expr",(ScicosF) evaluate_expr},
{"evtdly4",(ScicosF) evtdly4},
{"evtdly",(ScicosF) F2C(evtdly)},
{"evtvardly",(ScicosF) evtvardly},
{"expblk_m",(ScicosF) expblk_m},
{"expblk",(ScicosF) F2C(expblk)},
{"extdiag",(ScicosF) extdiag},
{"extdiagz",(ScicosF) extdiagz},
{"extract_bit_16_LH",(ScicosF) extract_bit_16_LH},
{"extract_bit_16_LSB",(ScicosF) extract_bit_16_LSB},
{"extract_bit_16_MSB0",(ScicosF) extract_bit_16_MSB0},
{"extract_bit_16_MSB1",(ScicosF) extract_bit_16_MSB1},
{"extract_bit_16_RB0",(ScicosF) extract_bit_16_RB0},
{"extract_bit_16_RB1",(ScicosF) extract_bit_16_RB1},
{"extract_bit_16_UH0",(ScicosF) extract_bit_16_UH0},
{"extract_bit_16_UH1",(ScicosF) extract_bit_16_UH1},
{"extract_bit_32_LH",(ScicosF) extract_bit_32_LH},
{"extract_bit_32_LSB",(ScicosF) extract_bit_32_LSB},
{"extract_bit_32_MSB0",(ScicosF) extract_bit_32_MSB0},
{"extract_bit_32_MSB1",(ScicosF) extract_bit_32_MSB1},
{"extract_bit_32_RB0",(ScicosF) extract_bit_32_RB0},
{"extract_bit_32_RB1",(ScicosF) extract_bit_32_RB1},
{"extract_bit_32_UH0",(ScicosF) extract_bit_32_UH0},
{"extract_bit_32_UH1",(ScicosF) extract_bit_32_UH1},
{"extract_bit_8_LH",(ScicosF) extract_bit_8_LH},
{"extract_bit_8_LSB",(ScicosF) extract_bit_8_LSB},
{"extract_bit_8_MSB0",(ScicosF) extract_bit_8_MSB0},
{"extract_bit_8_MSB1",(ScicosF) extract_bit_8_MSB1},
{"extract_bit_8_RB0",(ScicosF) extract_bit_8_RB0},
{"extract_bit_8_RB1",(ScicosF) extract_bit_8_RB1},
{"extract_bit_8_UH0",(ScicosF) extract_bit_8_UH0},
{"extract_bit_8_UH1",(ScicosF) extract_bit_8_UH1},
{"extract_bit_u16_MSB1",(ScicosF) extract_bit_u16_MSB1},
{"extract_bit_u16_RB1",(ScicosF) extract_bit_u16_RB1},
{"extract_bit_u16_UH1",(ScicosF) extract_bit_u16_UH1},
{"extract_bit_u32_MSB1",(ScicosF) extract_bit_u32_MSB1},
{"extract_bit_u32_RB1",(ScicosF) extract_bit_u32_RB1},
{"extract_bit_u32_UH1",(ScicosF) extract_bit_u32_UH1},
{"extract_bit_u8_MSB1",(ScicosF) extract_bit_u8_MSB1},
{"extract_bit_u8_RB1",(ScicosF) extract_bit_u8_RB1},
{"extract_bit_u8_UH1",(ScicosF) extract_bit_u8_UH1},
{"extractor",(ScicosF) extractor},
{"extract",(ScicosF) extract},
{"extractz",(ScicosF) extractz},
{"exttril",(ScicosF) exttril},
{"exttrilz",(ScicosF) exttrilz},
{"exttriu",(ScicosF) exttriu},
{"exttriuz",(ScicosF) exttriuz},
{"forblk",(ScicosF) F2C(forblk)},
{"foriterator",(ScicosF) foriterator},
{"fromws_c",(ScicosF) fromws_c},
{"fsv",(ScicosF) F2C(fsv)},
{"gainblk_i16e",(ScicosF) gainblk_i16e},
{"gainblk_i16n",(ScicosF) gainblk_i16n},
{"gainblk_i16s",(ScicosF) gainblk_i16s},
{"gainblk_i32e",(ScicosF) gainblk_i32e},
{"gainblk_i32n",(ScicosF) gainblk_i32n},
{"gainblk_i32s",(ScicosF) gainblk_i32s},
{"gainblk_i8e",(ScicosF) gainblk_i8e},
{"gainblk_i8n",(ScicosF) gainblk_i8n},
{"gainblk_i8s",(ScicosF) gainblk_i8s},
{"gainblk",(ScicosF) gainblk},
{"gainblk_ui16e",(ScicosF) gainblk_ui16e},
{"gainblk_ui16n",(ScicosF) gainblk_ui16n},
{"gainblk_ui16s",(ScicosF) gainblk_ui16s},
{"gainblk_ui32e",(ScicosF) gainblk_ui32e},
{"gainblk_ui32n",(ScicosF) gainblk_ui32n},
{"gainblk_ui32s",(ScicosF) gainblk_ui32s},
{"gainblk_ui8e",(ScicosF) gainblk_ui8e},
{"gainblk_ui8n",(ScicosF) gainblk_ui8n},
{"gainblk_ui8s",(ScicosF) gainblk_ui8s},
{"gain",(ScicosF) gain},
{"gensin",(ScicosF) F2C(gensin)},
{"gensqr",(ScicosF) F2C(gensqr)},
{"hltblk",(ScicosF) F2C(hltblk)},
{"hystheresis",(ScicosF) hystheresis},
{"ifthel",(ScicosF) F2C(ifthel)},
{"integral_func",(ScicosF) integral_func},
{"integralz_func",(ScicosF) integralz_func},
{"integr",(ScicosF) F2C(integr)},
{"intplt",(ScicosF) F2C(intplt)},
{"intpol",(ScicosF) F2C(intpol)},
{"intrp2",(ScicosF) F2C(intrp2)},
{"intrpl",(ScicosF) F2C(intrpl)},
{"invblk4",(ScicosF) invblk4},
{"invblk",(ScicosF) F2C(invblk)},
{"iocopy",(ScicosF) F2C(iocopy)},
{"logblk",(ScicosF) F2C(logblk)},
{"logicalop_i16",(ScicosF) logicalop_i16},
{"logicalop_i32",(ScicosF) logicalop_i32},
{"logicalop_i8",(ScicosF) logicalop_i8},
{"logicalop_m",(ScicosF) logicalop_m},
{"logicalop",(ScicosF) logicalop},
{"logicalop_ui16",(ScicosF) logicalop_ui16},
{"logicalop_ui32",(ScicosF) logicalop_ui32},
{"logicalop_ui8",(ScicosF) logicalop_ui8},
{"logic",(ScicosF) logic},
{"lookup2d",(ScicosF) lookup2d},
{"lookup_c",(ScicosF) lookup_c},
{"lookup",(ScicosF) F2C(lookup)},
{"lsplit",(ScicosF) F2C(lsplit)},
{"lusat",(ScicosF) F2C(lusat)},
{"mat_bksl",(ScicosF) mat_bksl},
{"matbyscal_e",(ScicosF) matbyscal_e},
{"matbyscal",(ScicosF) matbyscal},
{"matbyscal_s",(ScicosF) matbyscal_s},
{"mat_cath",(ScicosF) mat_cath},
{"mat_catv",(ScicosF) mat_catv},
{"mat_det",(ScicosF) mat_det},
{"mat_diag",(ScicosF) mat_diag},
{"mat_div",(ScicosF) mat_div},
{"mat_expm",(ScicosF) mat_expm},
{"mathermit_m",(ScicosF) mathermit_m},
{"mat_inv",(ScicosF) mat_inv},
{"mat_lu",(ScicosF) mat_lu},
{"matmul2_e",(ScicosF) matmul2_e},
{"matmul2_m",(ScicosF) matmul2_m},
{"matmul2_s",(ScicosF) matmul2_s},
{"matmul_i16e",(ScicosF) matmul_i16e},
{"matmul_i16n",(ScicosF) matmul_i16n},
{"matmul_i16s",(ScicosF) matmul_i16s},
{"matmul_i32e",(ScicosF) matmul_i32e},
{"matmul_i32n",(ScicosF) matmul_i32n},
{"matmul_i32s",(ScicosF) matmul_i32s},
{"matmul_i8e",(ScicosF) matmul_i8e},
{"matmul_i8n",(ScicosF) matmul_i8n},
{"matmul_i8s",(ScicosF) matmul_i8s},
{"matmul_m",(ScicosF) matmul_m},
{"matmul_ui16e",(ScicosF) matmul_ui16e},
{"matmul_ui16n",(ScicosF) matmul_ui16n},
{"matmul_ui16s",(ScicosF) matmul_ui16s},
{"matmul_ui32e",(ScicosF) matmul_ui32e},
{"matmul_ui32n",(ScicosF) matmul_ui32n},
{"matmul_ui32s",(ScicosF) matmul_ui32s},
{"matmul_ui8e",(ScicosF) matmul_ui8e},
{"matmul_ui8n",(ScicosF) matmul_ui8n},
{"matmul_ui8s",(ScicosF) matmul_ui8s},
{"mat_pinv",(ScicosF) mat_pinv},
{"mat_reshape",(ScicosF) mat_reshape},
{"mat_sing",(ScicosF) mat_sing},
{"mat_sqrt",(ScicosF) mat_sqrt},
{"mat_sumc",(ScicosF) mat_sumc},
{"mat_suml",(ScicosF) mat_suml},
{"mat_sum",(ScicosF) mat_sum},
{"mat_svd",(ScicosF) mat_svd},
{"mattran_m",(ScicosF) mattran_m},
{"mat_vps",(ScicosF) mat_vps},
{"mat_vpv",(ScicosF) mat_vpv},
{"matz_absc",(ScicosF) matz_absc},
{"matz_abs",(ScicosF) matz_abs},
{"matz_bksl",(ScicosF) matz_bksl},
{"matz_cath",(ScicosF) matz_cath},
{"matz_catv",(ScicosF) matz_catv},
{"matz_conj",(ScicosF) matz_conj},
{"matz_det",(ScicosF) matz_det},
{"matz_diag",(ScicosF) matz_diag},
{"matz_div",(ScicosF) matz_div},
{"matz_expm",(ScicosF) matz_expm},
{"matz_inv",(ScicosF) matz_inv},
{"matz_lu",(ScicosF) matz_lu},
{"matzmul2_m",(ScicosF) matzmul2_m},
{"matzmul_m",(ScicosF) matzmul_m},
{"matz_pinv",(ScicosF) matz_pinv},
{"matz_reimc",(ScicosF) matz_reimc},
{"matz_reim",(ScicosF) matz_reim},
{"matz_reshape",(ScicosF) matz_reshape},
{"matz_sing",(ScicosF) matz_sing},
{"matz_sqrt",(ScicosF) matz_sqrt},
{"matz_sumc",(ScicosF) matz_sumc},
{"matz_suml",(ScicosF) matz_suml},
{"matz_sum",(ScicosF) matz_sum},
{"matz_svd",(ScicosF) matz_svd},
{"matztran_m",(ScicosF) matztran_m},
{"matz_vps",(ScicosF) matz_vps},
{"matz_vpv",(ScicosF) matz_vpv},
{"maxblk",(ScicosF) F2C(maxblk)},
{"memo",(ScicosF) F2C(memo)},
{"mfclck",(ScicosF) F2C(mfclck)},
{"m_frequ",(ScicosF) m_frequ},
{"minblk",(ScicosF) F2C(minblk)},
{"minmax",(ScicosF) minmax},
{"modulo_count",(ScicosF) modulo_count},
{"mswitch",(ScicosF) mswitch},
{"multiplex",(ScicosF) multiplex},
{"mux",(ScicosF) F2C(mux)},
{"pload",(ScicosF) F2C(pload)},
{"plusblk",(ScicosF) plusblk},
{"powblk",(ScicosF) F2C(powblk)},
{"prod",(ScicosF) prod},
{"product",(ScicosF) product},
{"qzcel",(ScicosF) F2C(qzcel)},
{"qzflr",(ScicosF) F2C(qzflr)},
{"qzrnd",(ScicosF) F2C(qzrnd)},
{"qztrn",(ScicosF) F2C(qztrn)},
{"ramp",(ScicosF) ramp},
{"ratelimiter",(ScicosF) ratelimiter},
{"readau",(ScicosF) readau},
{"readc",(ScicosF) readc},
{"readf",(ScicosF) F2C(readf)},
{"relational_op_i16",(ScicosF) relational_op_i16},
{"relational_op_i32",(ScicosF) relational_op_i32},
{"relational_op_i8",(ScicosF) relational_op_i8},
{"relational_op",(ScicosF) relational_op},
{"relationalop",(ScicosF) relationalop},
{"relational_op_ui16",(ScicosF) relational_op_ui16},
{"relational_op_ui32",(ScicosF) relational_op_ui32},
{"relational_op_ui8",(ScicosF) relational_op_ui8},
{"relay",(ScicosF) relay},
{"ricc_m",(ScicosF) ricc_m},
{"rndblk_m",(ScicosF) rndblk_m},
{"rndblk",(ScicosF) F2C(rndblk)},
{"rndblkz_m",(ScicosF) rndblkz_m},
{"root_coef",(ScicosF) root_coef},
{"rootz_coef",(ScicosF) rootz_coef},
{"samphold4_m",(ScicosF) samphold4_m},
{"samphold4",(ScicosF) samphold4},
{"samphold",(ScicosF) F2C(samphold)},
{"satur",(ScicosF) satur},
{"sawtth",(ScicosF) F2C(sawtth)},
{"scalar2vector",(ScicosF) scalar2vector},
{"sciblk",(ScicosF) F2C(sciblk)},
{"scicosexit",(ScicosF) scicosexit},
{"selblk",(ScicosF) F2C(selblk)},
{"selector_m",(ScicosF) selector_m},
{"selector",(ScicosF) selector},
{"shift_16_LA",(ScicosF) shift_16_LA},
{"shift_16_LC",(ScicosF) shift_16_LC},
{"shift_16_RA",(ScicosF) shift_16_RA},
{"shift_16_RC",(ScicosF) shift_16_RC},
{"shift_32_LA",(ScicosF) shift_32_LA},
{"shift_32_LC",(ScicosF) shift_32_LC},
{"shift_32_RA",(ScicosF) shift_32_RA},
{"shift_32_RC",(ScicosF) shift_32_RC},
{"shift_8_LA",(ScicosF) shift_8_LA},
{"shift_8_LC",(ScicosF) shift_8_LC},
{"shift_8_RA",(ScicosF) shift_8_RA},
{"shift_8_RC",(ScicosF) shift_8_RC},
{"shift_u16_RA",(ScicosF) shift_u16_RA},
{"shift_u32_RA",(ScicosF) shift_u32_RA},
{"shift_u8_RA",(ScicosF) shift_u8_RA},
{"signum",(ScicosF) signum},
{"sinblk",(ScicosF) F2C(sinblk)},
{"sin_blk",(ScicosF) sin_blk},
{"sinh_blk",(ScicosF) sinh_blk},
{"sqrblk",(ScicosF) F2C(sqrblk)},
{"step_func",(ScicosF) step_func},
{"submat",(ScicosF) submat},
{"submatz",(ScicosF) submatz},
{"sum2",(ScicosF) F2C(sum2)},
{"sum3",(ScicosF) F2C(sum3)},
{"summation_i16e",(ScicosF) summation_i16e},
{"summation_i16n",(ScicosF) summation_i16n},
{"summation_i16s",(ScicosF) summation_i16s},
{"summation_i32e",(ScicosF) summation_i32e},
{"summation_i32n",(ScicosF) summation_i32n},
{"summation_i32s",(ScicosF) summation_i32s},
{"summation_i8e",(ScicosF) summation_i8e},
{"summation_i8n",(ScicosF) summation_i8n},
{"summation_i8s",(ScicosF) summation_i8s},
{"summation",(ScicosF) summation},
{"summation_ui16e",(ScicosF) summation_ui16e},
{"summation_ui16n",(ScicosF) summation_ui16n},
{"summation_ui16s",(ScicosF) summation_ui16s},
{"summation_ui32e",(ScicosF) summation_ui32e},
{"summation_ui32n",(ScicosF) summation_ui32n},
{"summation_ui32s",(ScicosF) summation_ui32s},
{"summation_ui8e",(ScicosF) summation_ui8e},
{"summation_ui8n",(ScicosF) summation_ui8n},
{"summation_ui8s",(ScicosF) summation_ui8s},
{"summation_z",(ScicosF) summation_z},
{"sum",(ScicosF) sum},
{"switch2_m",(ScicosF) switch2_m},
{"switch2",(ScicosF) switch2},
{"switchn",(ScicosF) switchn},
{"tablex2d_c",(ScicosF) tablex2d_c},
{"tanblk",(ScicosF) F2C(tanblk)},
{"tan_blk",(ScicosF) tan_blk},
{"tanh_blk",(ScicosF) tanh_blk},
{"tcslti4",(ScicosF) tcslti4},
{"tcslti",(ScicosF) F2C(tcslti)},
{"tcsltj4",(ScicosF) tcsltj4},
{"tcsltj",(ScicosF) F2C(tcsltj)},
{"timblk",(ScicosF) F2C(timblk)},
{"time_delay",(ScicosF) time_delay},
{"tows_c",(ScicosF) tows_c},
{"trash",(ScicosF) F2C(trash)},
{"variable_delay",(ScicosF) variable_delay},
{"whileiterator",(ScicosF) whileiterator},
{"writeau",(ScicosF) writeau},
{"writec",(ScicosF) writec},
{"writef",(ScicosF) F2C(writef)},
{"zcross2",(ScicosF) zcross2},
{"zcross",(ScicosF) F2C(zcross)},
{(char *) 0, (ScicosF) 0}};
 
int ntabsim= 369 ;
#endif 
/***********************************/
