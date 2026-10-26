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
    assert(n == 3);
    assert(!strcmp(it[0].title, "Big & Bold News"));
    assert(!strcmp(it[0].body, "First line second <ok> ' ' caf More"));
    assert(!strcmp(it[0].source, "Src"));
    assert(!strcmp(it[1].title, "Second") && !strcmp(it[1].body, "Body two line"));
    assert(!strcmp(it[2].body, "end of it < 3")); /* "Empty" has no text: not an item */
    assert(news_parse_html(html, "S", it, 2) == 2);
    assert(news_parse_html("", "S", it, 2) == 0);
    /* the shape of dc.dreamcastlive.net/news.html */
    const char* dcl =
        "<html><body bgcolor=\"#000000\"><center><table><tr><td><a href=\"./index.html\"><img src=\"./header.gif\" alt=\"\"></a></td></tr></table></center>"
        "<h1><font color=\"white\">News</font></h1>\n\t\t<h3><font color=\"white\">NFL 2K1 Roster Update Now Available!</font></h3>\n<p>\n<font color=\"white\">You can get the roster. \n"
        "Big thanks to Xiden!\n\n<br><br>- Posted by pcwzrd13 on October 31st, 2024\n</font>\n</p>\n"
        "<h3><font color=\"white\">Shutokou Battle 1 & 2 DLC Added!</font></h3><p><font color=\"white\">DLC added. <br><br>- Posted by pcwzrd13 on August 22nd, 2020</font></p></body></html>";
    n = news_parse_html(dcl, "DreamcastLive", it, NEWS_MAX);
    assert(n == 2);
    assert(!strcmp(it[0].title, "NFL 2K1 Roster Update Now Available!"));
    assert(!strcmp(it[0].body, "You can get the roster. Big thanks to Xiden!"));
    assert(!strcmp(it[0].when, "October 31st, 2024  pcwzrd13"));
    assert(!strcmp(it[1].title, "Shutokou Battle 1 & 2 DLC Added!") && !strcmp(it[1].body, "DLC added."));
    puts("news_parse ok");
    return 0;
}
