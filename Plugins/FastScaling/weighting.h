#include "Stdafx.h"
#pragma once
#pragma unmanaged
#include "shared.h"

static void derive_cubic_coefficients(double B, double C, InterpolationDetailsPtr out){
    double bx2 = B + B;
    out->p1 = 1.0 - (1.0 / 3.0)*B;
    out->p2 = -3.0 + bx2 + C;
    out->p3 = 2.0 - 1.5*B - C;
    out->q1 = (4.0 / 3.0)*B + 4.0*C;
    out->q2 = -8.0*C - bx2;
    out->q3 = B + 5.0*C;
    out->q4 = (-1.0 / 6.0)*B - C;
}


static inline double filter_flex_cubic(const InterpolationDetailsPtr d, double x)
{
    
    const double t = (double)fabs(x) / d->blur;

    if (t < 1.0){
        return (d->p1 + t * (t* (d->p2 + t*d->p3)));
    }
    if (t < 2.0){
        return(d->q1 + t*(d->q2 + t* (d->q3 + t*d->q4)));
    }
    return(0.0);
}
static inline double filter_bicubic_fast(const InterpolationDetailsPtr d, const double t)
{
    const double abs_t = (double)fabs(t) / d->blur;
    const double abs_t_sq = abs_t * abs_t;
    if (abs_t<1) return 1 - 2 * abs_t_sq + abs_t_sq*abs_t;
    if (abs_t<2) return d->negative_multiplier * (4 - 8 * abs_t + 5 * abs_t_sq - abs_t_sq*abs_t);
    return 0;
}


static inline double filter_lanczos(const InterpolationDetailsPtr d, double t)
{
    const double width = d->filter_var_a;

    const double abs_t = (double)fabs(t) / d->blur;
    if (abs_t < width)	{
        return (IR_SINC(abs_t) * IR_SINC(abs_t / width));
    }
    return 0;
}

static inline double filter_sinc(const InterpolationDetailsPtr d, double t)
{

    const double abs_t = (double)fabs(t) / d->blur;
    if (abs_t == 0) { return 1; } //Avoid division by zero
    if (abs_t > 2){ return 0; }
    const double a = abs_t * IR_PI;
    const double r = sin(a) / a;
    return r > 0 ? r : d->negative_multiplier * r;
}



static InterpolationDetailsPtr CreateBicubicCustom(double window, double blur, double B, double C){
    InterpolationDetailsPtr d = CreateInterpolationDetails();
    d->blur = blur;
    derive_cubic_coefficients(B, C, d);
    d->filter = filter_flex_cubic;
    d->window = window;
    return d;
}

static InterpolationDetailsPtr DetailsLanczosCustom(double window, double blur, double width){
    InterpolationDetailsPtr d = CreateInterpolationDetails();
    d->blur = blur;
    d->filter = filter_lanczos;
    d->window = window;
    d->filter_var_a = width;
    return d;
}
static InterpolationDetailsPtr DetailsLanczosSharp(){
    InterpolationDetailsPtr d = CreateInterpolationDetails();
    d->blur = 0.9549963639785485;
    d->filter = filter_sinc;
    d->window = 3;
    return d;
}

static InterpolationDetailsPtr DetailsLanczos(){
    return DetailsLanczosCustom(2, 1,3);
}

static InterpolationDetailsPtr DetailsOriginal(){
    InterpolationDetailsPtr d = CreateInterpolationDetails();
    d->blur = 1;
    d->filter = filter_bicubic_fast;
    d->window = 1;
    return d;
}

static InterpolationDetailsPtr DetailsDefault(){
    return CreateBicubicCustom(1, 1, 1, 0);
}

static InterpolationDetailsPtr DetailsGeneralCubic(){
    return CreateBicubicCustom(1, 2, 1, 0);
}
static InterpolationDetailsPtr DetailsCatmullRom(){
    return CreateBicubicCustom(1, 2, 0, 0.5);
}
static InterpolationDetailsPtr DetailsMitchell(){
    return CreateBicubicCustom(2, 8.0 / 7.0, 1. / 3., 1. / 3.);
}
static InterpolationDetailsPtr DetailsRobidoux(){
    return CreateBicubicCustom(2, 1.1685777620836932,
        0.37821575509399867, 0.31089212245300067);
}

static InterpolationDetailsPtr DetailsRobidouxSharp(){
    return CreateBicubicCustom(1, 1.105822933719019,
        0.2620145123990142, 0.3689927438004929);
}
static InterpolationDetailsPtr DetailsHermite(){
    return CreateBicubicCustom(1, 2, 1, 0);
}






static inline LineContribType *  ContributionsAlloc(unsigned int line_length, unsigned int windows_size)
{
    unsigned int u = 0;
    LineContribType *res;

    res = (LineContribType *)malloc(sizeof(LineContribType));
    if (!res) {
        return NULL;
    }
    res->WindowSize = windows_size;
    res->LineLength = line_length;
    res->ContribRow = (ContributionType *)malloc(line_length * sizeof(ContributionType));


    float *allWeights = (float *)calloc(windows_size * line_length, sizeof(float));

    for (int i = 0; i < line_length; i++)
        res->ContribRow[i].Weights = allWeights + (i * windows_size);

    return res;
}

static inline void ContributionsFree(LineContribType * p)
{
    free(p->ContribRow[0].Weights);
    free(p->ContribRow);
    free(p);
}


#define TONY 0.00001

static inline LineContribType *ContributionsCalc(unsigned int line_size, unsigned int src_size, const InterpolationDetailsPtr details)
{
    const double scale_factor = (double)line_size / (double)src_size;
    const double downscale_factor = MIN(1.0, scale_factor);
    const double half_source_window = details->window * 0.5 / downscale_factor;

    const int allocated_window_size = (int)ceil(2 * (half_source_window - TONY)) + 1;
    unsigned int u, ix;
    LineContribType *res = ContributionsAlloc(line_size, allocated_window_size);

    for (u = 0; u < line_size; u++) {
        const double center_src_pixel = ((double)u + 0.5) / scale_factor - 0.5;

        const int left_edge = (int)ceil(center_src_pixel - half_source_window - 0.5 + TONY);
        const int right_edge = (int)floor(center_src_pixel + half_source_window + 0.5 - TONY);

        const int left_src_pixel = MAX(0, left_edge);
        const int right_src_pixel = MIN(right_edge, (int)src_size - 1);

        double total_weight = 0.0;
        double positive_weight = 0.0;
        double negative_weight = 0.0;

        const int source_pixel_count = right_src_pixel - left_src_pixel + 1;

        if (source_pixel_count > allocated_window_size){
            ContributionsFree(res);
            exit(200);
            return NULL;
        }

        res->ContribRow[u].Left = left_src_pixel;
        res->ContribRow[u].Right = right_src_pixel;


        float *weights = res->ContribRow[u].Weights;

        //commented: additional weight for edges (doesn't seem to be too effective)
        //for (ix = left_edge; ix <= right_edge; ix++) {
        for (ix = left_src_pixel; ix <= right_src_pixel; ix++) {
            int tx = ix - left_src_pixel;
            //int tx = MIN(MAX(ix, left_src_pixel), right_src_pixel) - left_src_pixel;
            double add = (*details->filter)(details, downscale_factor * ((double)ix - center_src_pixel));

            weights[tx] = add;
            total_weight += add;
            if (add > 0) positive_weight += add;
            if (add < 0) negative_weight -= add;
        }

        if (total_weight <= TONY) {
            ContributionsFree(res);
            return NULL;
        }

        double negative_factor = 1 / total_weight;
        if (details->integrated_sharpen_percent > 0){
            const double new_neg_weight = (details->integrated_sharpen_percent / 100.0 * positive_weight);
            total_weight -= new_neg_weight - negative_weight;

            negative_factor = (new_neg_weight / negative_weight) / total_weight;
            negative_weight = new_neg_weight;

        }
        double positive_factor = 1 / total_weight;

        float *weigths;
        for (ix = 0; ix < source_pixel_count; ix++) {
            weights[ix] = weights[ix] > 0 ? weights[ix] * positive_factor : weights[ix] * negative_factor;
        }
    }
    return res;
}


