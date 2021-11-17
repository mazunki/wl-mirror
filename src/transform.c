#include <string.h>
#include <wayland-client-protocol.h>
#include "transform.h"

static const mat3_t mat_identity = { .data = {
    {  1,  0,  0 },
    {  0,  1,  0 },
    {  0,  0,  1 }
}};

static const mat3_t mat_rot_ccw_90 = { .data = {
    {  0,  1,  0 },
    { -1,  0,  1 },
    {  0,  0,  1 }
}};

static const mat3_t mat_rot_ccw_180 = { .data = {
    { -1,  0,  1 },
    {  0, -1,  1 },
    {  0,  0,  1 }
}};

static const mat3_t mat_rot_ccw_270 = { .data = {
    {  0, -1,  1 },
    {  1,  0,  0 },
    {  0,  0,  1 }
}};

static const mat3_t mat_flip_x = { .data = {
    { -1,  0,  1 },
    {  0,  1,  0 },
    {  0,  0,  1 }
}};

static const mat3_t mat_flip_y = { .data = {
    {  1,  0,  0 },
    {  0, -1,  1 },
    {  0,  0,  1 }
}};

void mat3_identity(mat3_t * mat) {
    *mat = mat_identity;
}

void mat3_transpose(mat3_t * mat) {
    for (size_t row = 0; row < 3; row++) {
        for (size_t col = row + 1; col < 3; col++) {
            float temp = mat->data[row][col];
            mat->data[row][col] = mat->data[col][row];
            mat->data[col][row] = temp;
        }
    }
}

void mat3_mul(const mat3_t * mul, mat3_t * dest) {
    mat3_t src = *dest;

    for (size_t row = 0; row < 3; row++) {
        for (size_t col = 0; col < 3; col++) {
            dest->data[row][col] = 0;
            for (size_t i = 0; i < 3; i++) {
                dest->data[row][col] += mul->data[row][i] * src.data[i][col];
            }
        }
    }
}

void mat3_apply_transform(mat3_t * mat, transform_t transform) {
    if (transform.flip_x) mat3_mul(&mat_flip_x, mat);
    if (transform.flip_y) mat3_mul(&mat_flip_y, mat);

    switch (transform.rotation) {
        case ROT_NORMAL:
            break;
        case ROT_CCW_90:
            mat3_mul(&mat_rot_ccw_90, mat);
            break;
        case ROT_CCW_180:
            mat3_mul(&mat_rot_ccw_180, mat);
            break;
        case ROT_CCW_270:
            mat3_mul(&mat_rot_ccw_270, mat);
            break;
    }
}

void mat3_apply_wayland_transform(mat3_t * mat, enum wl_output_transform transform) {
    switch (transform) {
        case WL_OUTPUT_TRANSFORM_NORMAL:
            break;
        case WL_OUTPUT_TRANSFORM_90:
            mat3_mul(&mat_rot_ccw_90, mat);
            break;
        case WL_OUTPUT_TRANSFORM_180:
            mat3_mul(&mat_rot_ccw_180, mat);
            break;
        case WL_OUTPUT_TRANSFORM_270:
            mat3_mul(&mat_rot_ccw_270, mat);
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED:
            mat3_mul(&mat_flip_x, mat);
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:
            mat3_mul(&mat_flip_x, mat);
            mat3_mul(&mat_rot_ccw_90, mat);
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_180:
            mat3_mul(&mat_flip_x, mat);
            mat3_mul(&mat_rot_ccw_180, mat);
            break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_270:
            mat3_mul(&mat_flip_x, mat);
            mat3_mul(&mat_rot_ccw_270, mat);
            break;
    }
}

void mat3_apply_invert_y(mat3_t * mat, bool invert_y) {
    if (invert_y) {
        mat3_mul(&mat_flip_y, mat);
    }
}
