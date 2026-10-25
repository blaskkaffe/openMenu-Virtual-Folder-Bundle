/*
 * File: news_parse.h
 * Project: openmenu
 * News items out of a plain HTML page for the Internet screen: every heading (h1..h4) starts an item, the text after it up to the
 * next heading is its body. No console dependencies, host-tested (test/news_parse_test.c).
 */
#pragma once

#define NEWS_MAX        24
#define NEWS_TITLE_LEN  72
#define NEWS_BODY_LEN   600
#define NEWS_SOURCE_LEN 24

typedef struct news_item {
    char title[NEWS_TITLE_LEN];
    char body[NEWS_BODY_LEN];
    char source[NEWS_SOURCE_LEN];
} news_item_t;

/* Parses `html` (NUL terminated) into out[0..max), tagging the items with `source`. Returns how many items it found. */
int news_parse_html(const char* html, const char* source, news_item_t* out, int max);
