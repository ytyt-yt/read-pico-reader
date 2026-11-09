/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover.h"
static unsigned polls, cancel_at;
static bool active(void) { return ++polls < cancel_at; }
int main(int argc, char **argv) {
    assert(argc > 2);
    for (int i = 2; i < argc; ++i) {
        bool png = strstr(argv[i], ".png") != NULL;
        unsigned w = 0, h = 0;
        const unsigned ow = 176, oh = 240;
        uint8_t *pixels = malloc((size_t)ow * oh + 16);
        assert(pixels);
        memset(pixels, 0xcc, (size_t)ow * oh + 16);
        if (!strcmp(argv[1], "reject")) {
            assert(!book_image_file_grayscale(argv[i], png, ow, oh, pixels));
        } else {
            assert(book_image_file_dimensions(argv[i], png, &w, &h));
            assert(book_image_file_grayscale(argv[i], png, ow, oh, pixels));
            unsigned sum = 0;
            for (unsigned j = 0; j < ow * oh; ++j) sum += pixels[j];
            assert(sum > 30u * ow * oh && sum < 230u * ow * oh);
            uint8_t *reference = malloc((size_t)ow * oh);
            assert(reference);
            memcpy(reference, pixels, (size_t)ow * oh);
            polls = 0; cancel_at = UINT32_MAX;
            assert(book_image_file_grayscale_active(argv[i], png, ow, oh, pixels, active));
            assert(!memcmp(reference, pixels, (size_t)ow * oh));
            unsigned total = polls;
            assert(total > 4);
            // 分别在入口和解码途中取消，随后完整重试；取消不得启动第二个解码器。
            // Cancel at entry and mid-decode, then retry completely; cancellation must not start a fallback codec.
            for (unsigned pass = 0; pass < 2; ++pass) {
                polls = 0; cancel_at = pass ? total / 2 : 1;
                memset(pixels, 0xcc, (size_t)ow * oh);
                assert(!book_image_file_grayscale_active(argv[i], png, ow, oh, pixels, active));
                assert(polls >= cancel_at && polls <= cancel_at + 4);
                if (!pass) for (unsigned j = 0; j < ow * oh; ++j) assert(pixels[j] == 0xcc);
            }
            polls = 0; cancel_at = UINT32_MAX;
            assert(book_image_file_grayscale_active(argv[i], png, ow, oh, pixels, active));
            assert(!memcmp(reference, pixels, (size_t)ow * oh));
            free(reference);
            printf("decoded: %s (%ux%u)\n", argv[i], w, h);
        }
        for (unsigned j = 0; j < 16; ++j) assert(pixels[(size_t)ow * oh + j] == 0xcc);
        free(pixels);
    }
}
