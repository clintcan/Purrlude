#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/TimelineView.h"

using Catch::Matchers::WithinAbs;
using purrlude::TimelineView;

TEST_CASE ("The timeline fits the whole song until zoomed", "[timeline]")
{
    TimelineView view;
    view.update (64.0, 640.0);   // 16 bars in 640 pixels

    REQUIRE (view.isFit());
    REQUIRE_THAT (view.getPixelsPerBeat(), WithinAbs (10.0, 1e-9));
    REQUIRE_THAT (view.getStartBeat(), WithinAbs (0.0, 1e-9));

    // The song grows: still all of it.
    view.update (128.0, 640.0);
    REQUIRE_THAT (view.getPixelsPerBeat(), WithinAbs (5.0, 1e-9));

    // Scrolling does nothing while everything is in view.
    view.scrollTo (20.0, 128.0, 640.0);
    REQUIRE_THAT (view.getStartBeat(), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("Zooming keeps the anchor where it is", "[timeline]")
{
    TimelineView view;
    view.update (128.0, 640.0);           // 5 px per beat
    auto anchorX = view.beatToX (40.0);   // 200 px

    view.zoom (4.0, 40.0, 128.0, 640.0);
    REQUIRE_FALSE (view.isFit());
    REQUIRE_THAT (view.getPixelsPerBeat(), WithinAbs (20.0, 1e-9));
    REQUIRE_THAT (view.beatToX (40.0), WithinAbs (anchorX, 1e-6));
    REQUIRE_THAT (view.getVisibleBeats (640.0), WithinAbs (32.0, 1e-9));

    // Scrolling stays within the song.
    view.scrollTo (500.0, 128.0, 640.0);
    REQUIRE_THAT (view.getStartBeat(), WithinAbs (96.0, 1e-9));   // the last 32 beats
    view.scrollTo (-10.0, 128.0, 640.0);
    REQUIRE_THAT (view.getStartBeat(), WithinAbs (0.0, 1e-9));

    // Zooming in near the start can't scroll before it.
    view.zoom (2.0, 0.0, 128.0, 640.0);
    REQUIRE_THAT (view.getStartBeat(), WithinAbs (0.0, 1e-9));
    REQUIRE_THAT (view.xToBeat (view.beatToX (12.5)), WithinAbs (12.5, 1e-9));
}

TEST_CASE ("Zooming out past the whole song is fit mode again", "[timeline]")
{
    TimelineView view;
    view.update (128.0, 640.0);
    view.zoom (8.0, 64.0, 128.0, 640.0);
    REQUIRE_FALSE (view.isFit());

    view.zoom (1.0 / 100.0, 64.0, 128.0, 640.0);
    REQUIRE (view.isFit());
    REQUIRE_THAT (view.getStartBeat(), WithinAbs (0.0, 1e-9));

    // And there's a limit to zooming in.
    view.zoom (1.0e6, 10.0, 128.0, 640.0);
    REQUIRE_THAT (view.getPixelsPerBeat(), WithinAbs (TimelineView::maxPixelsPerBeat, 1e-9));

    view.zoomToFit();
    view.update (128.0, 640.0);
    REQUIRE (view.isFit());
    REQUIRE_THAT (view.getPixelsPerBeat(), WithinAbs (5.0, 1e-9));
}
