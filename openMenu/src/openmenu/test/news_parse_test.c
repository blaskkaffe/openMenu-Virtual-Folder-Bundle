/* Host test: gcc -I src/openmenu/src src/openmenu/src/backend/news_parse.c src/openmenu/test/news_parse_test.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "backend/news_parse.h"

int
main(void) {
    static news_item_t it[NEWS_MAX];
    const char* html =
        "<html><head><style>h2{x}</style><script>var a='<h2>no</h2>';</script></head><body>intro text<!-- <h2>c</h2> -->"
        "<h2 class=\"t\">Big &amp; <b>Bold</b> News</h2><p>First  line\n second &lt;ok&gt; &#8217; \xE2\x80\x99 caf\xC3\xA9</p><p>More</p>"
        "<H3>Second</H3>Body two<br>line<h1>Empty</h1><hr><h4>Last</h4>end <i>of</i> it < 3</body></html>";
    int n = news_parse_html(html, "Src", it, NEWS_MAX);
    assert(n == 4);
    assert(!strcmp(it[0].title, "Big & Bold News"));
    assert(!strcmp(it[0].body, "First line second <ok> ' ' caf More"));
    assert(!strcmp(it[0].source, "Src"));
    assert(!strcmp(it[1].title, "Second") && !strcmp(it[1].body, "Body two line"));
    assert(!strcmp(it[2].title, "Empty") && it[2].body[0] == '\0');
    assert(!strcmp(it[3].body, "end of it < 3"));
    assert(news_parse_html(html, "S", it, 2) == 2);
    assert(news_parse_html("", "S", it, 2) == 0);
    puts("news_parse ok");
    return 0;
}
