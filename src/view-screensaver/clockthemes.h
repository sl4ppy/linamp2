#ifndef CLOCKTHEMES_H
#define CLOCKTHEMES_H

#include <QColor>
#include <QPointF>
#include <QVector>
#include <QPainterPath>
#include <cmath>

// --- Enums ---

enum class HandShape {
    Tapered,
    Sword,
    Mercedes,
    Dauphine,
    Needle,
    Breguet,
    Alpha
};

enum class TickShape {
    None,
    Line,
    Rect,
    Triangle,
    Dot,
    Diamond
};

enum class NumeralStyle {
    None,
    Arabic,
    Roman
};

// --- Structs ---

struct ClockColors {
    QColor dial;           // dial background fill
    QColor rim;            // outer rim stroke
    QColor hourHand;
    QColor minuteHand;
    QColor secondHand;
    QColor ticks;          // primary tick color
    QColor cardinalTicks;  // 12/3/6/9 tick color (if different)
    QColor numerals;
    QColor lume;           // luminous dot color
    QColor centerPin;
    QColor decorativeRing;
};

struct TickConfig {
    TickShape cardinalShape = TickShape::Triangle;   // 12, 3, 6, 9
    TickShape hourShape     = TickShape::Rect;       // other hour marks
    TickShape minuteShape   = TickShape::Line;       // minute marks
    float cardinalSize      = 1.0f;   // relative size multiplier
    float hourSize          = 1.0f;
    float minuteSize        = 1.0f;
    bool luminousDots       = false;  // draw lume dots at hour positions
};

struct HandConfig {
    HandShape hourShape    = HandShape::Tapered;
    HandShape minuteShape  = HandShape::Tapered;
    HandShape secondShape  = HandShape::Needle;
    float hourLength       = 0.50f;   // fraction of dial radius
    float minuteLength     = 0.78f;
    float secondLength     = 0.85f;
    float hourWidth        = 0.08f;   // fraction of dial radius
    float minuteWidth      = 0.06f;
    float secondWidth      = 0.02f;
    bool secondCounterweight = false;
    float counterweightLen = 0.25f;   // fraction of dial radius
};

struct ClockTheme {
    const char *name;
    ClockColors colors;
    HandConfig hands;
    TickConfig ticks;

    NumeralStyle numeralStyle   = NumeralStyle::None;
    float numeralSizeFraction   = 0.12f;  // font size as fraction of radius

    float dialRadiusFraction    = 0.42f;  // radius = min(W,H) * this
    int decorativeRings         = 0;      // number of concentric rings inside dial
    bool tachymeterRing         = false;

    bool isDigital              = false;  // true = use digital clock renderer
    bool outlineOnly            = false;  // hands drawn as outlines (no fill)
    bool hueCycling             = false;
    float glowIntensity         = 1.0f;
    float breatheAmount         = 0.25f;  // 0 = no breathing, 0.25 = default
};

// --- Hand Polygon Generators ---
// All generate shapes pointing UP (tip at negative Y), pivot at origin.
// Caller scales by hand length and rotates to correct angle.

inline QVector<QPointF> generateTaperedHand(float length, float width)
{
    float hw = width * 0.5f;
    return {
        { 0.0f, -length },          // tip
        { hw * 0.3f, -length * 0.9f },
        { hw, 0.0f },               // base right
        { -hw, 0.0f },              // base left
        { -hw * 0.3f, -length * 0.9f }
    };
}

inline QVector<QPointF> generateSwordHand(float length, float width)
{
    float hw = width * 0.5f;
    float bw = hw * 0.4f;
    return {
        { 0.0f, -length },           // tip
        { hw * 0.25f, -length * 0.92f },
        { hw, -length * 0.55f },     // widest point
        { hw * 0.7f, -length * 0.15f },
        { bw, 0.0f },                // base right
        { -bw, 0.0f },               // base left
        { -hw * 0.7f, -length * 0.15f },
        { -hw, -length * 0.55f },    // widest point left
        { -hw * 0.25f, -length * 0.92f }
    };
}

inline QVector<QPointF> generateMercedesHand(float length, float width)
{
    // Main body - short stubby with a circle cutout near the base
    float hw = width * 0.5f;
    return {
        { 0.0f, -length },
        { hw * 0.35f, -length * 0.9f },
        { hw, -length * 0.5f },
        { hw, -length * 0.15f },
        { hw * 0.8f, 0.0f },
        { -hw * 0.8f, 0.0f },
        { -hw, -length * 0.15f },
        { -hw, -length * 0.5f },
        { -hw * 0.35f, -length * 0.9f }
    };
}

// Returns the circle center (relative) and radius for the Mercedes cutout
inline void getMercedesCircle(float length, float /*width*/, QPointF &center, float &radius)
{
    center = QPointF(0.0f, -length * 0.35f);
    radius = length * 0.1f;
}

inline QVector<QPointF> generateDauphineHand(float length, float width)
{
    float hw = width * 0.5f;
    return {
        { 0.0f, -length },                // tip
        { hw * 0.2f, -length * 0.85f },
        { hw, -length * 0.45f },          // widest right
        { hw * 0.6f, -length * 0.1f },
        { hw * 0.3f, 0.0f },              // base right
        { -hw * 0.3f, 0.0f },             // base left
        { -hw * 0.6f, -length * 0.1f },
        { -hw, -length * 0.45f }           // widest left
    };
}

inline QVector<QPointF> generateNeedleHand(float length, float width)
{
    float hw = width * 0.35f;
    return {
        { 0.0f, -length },
        { hw * 0.15f, -length * 0.85f },
        { hw, 0.0f },
        { -hw, 0.0f },
        { -hw * 0.15f, -length * 0.85f }
    };
}

inline QVector<QPointF> generateBreguetHand(float length, float width)
{
    float hw = width * 0.45f;
    float bw = hw * 0.4f;
    return {
        { 0.0f, -length },
        { hw * 0.15f, -length * 0.92f },
        { hw * 0.5f, -length * 0.78f },  // widen for moon-hole area
        { hw * 0.5f, -length * 0.62f },
        { hw * 0.15f, -length * 0.55f }, // narrow again
        { hw * 0.6f, -length * 0.3f },   // widest
        { bw, 0.0f },
        { -bw, 0.0f },
        { -hw * 0.6f, -length * 0.3f }
    };
}

inline void getBreguetMoonHole(float length, float /*width*/, QPointF &center, float &radius)
{
    center = QPointF(0.0f, -length * 0.70f);
    radius = length * 0.055f;
}

inline QVector<QPointF> generateAlphaHand(float length, float width)
{
    // Leaf/alpha shape with smooth sinusoidal bulge — 26 vertices
    QVector<QPointF> pts;
    pts.reserve(26);
    const int N = 13;
    for (int i = 0; i <= N; i++) {
        float t = static_cast<float>(i) / N;  // 0..1 from base to tip
        float bulge = sinf(t * static_cast<float>(M_PI));
        float x = width * 0.5f * bulge;
        float y = -length * t;
        pts.append({x, y});
    }
    for (int i = N; i >= 0; i--) {
        float t = static_cast<float>(i) / N;
        float bulge = sinf(t * static_cast<float>(M_PI));
        float x = -width * 0.5f * bulge;
        float y = -length * t;
        if (i < N) pts.append({x, y}); // skip duplicate tip
    }
    return pts;
}

// Dispatch to correct generator
inline QVector<QPointF> generateHandPolygon(HandShape shape, float length, float width)
{
    switch (shape) {
    case HandShape::Tapered:   return generateTaperedHand(length, width);
    case HandShape::Sword:     return generateSwordHand(length, width);
    case HandShape::Mercedes:  return generateMercedesHand(length, width);
    case HandShape::Dauphine:  return generateDauphineHand(length, width);
    case HandShape::Needle:    return generateNeedleHand(length, width);
    case HandShape::Breguet:   return generateBreguetHand(length, width);
    case HandShape::Alpha:     return generateAlphaHand(length, width);
    }
    return generateTaperedHand(length, width);
}

// --- Counterweight tail (for second hands) ---
inline QVector<QPointF> generateCounterweight(float length, float width)
{
    float hw = width * 0.6f;
    return {
        { 0.0f, length * 0.85f },    // tail tip
        { hw, length * 0.15f },
        { hw * 0.4f, 0.0f },
        { -hw * 0.4f, 0.0f },
        { -hw, length * 0.15f }
    };
}

// --- Theme Definitions ---

inline ClockTheme makeLuxuryTheme()
{
    ClockTheme t;
    t.name = "Luxury";
    t.colors.dial           = QColor(15, 25, 60);
    t.colors.rim            = QColor(200, 170, 80);
    t.colors.hourHand       = QColor(200, 170, 80);
    t.colors.minuteHand     = QColor(200, 170, 80);
    t.colors.secondHand     = QColor(180, 180, 180);
    t.colors.ticks          = QColor(200, 170, 80);
    t.colors.cardinalTicks  = QColor(220, 190, 90);
    t.colors.numerals       = QColor(200, 170, 80);
    t.colors.lume           = QColor(100, 220, 120);
    t.colors.centerPin      = QColor(200, 170, 80);
    t.colors.decorativeRing = QColor(200, 170, 80, 60);

    t.hands.hourShape     = HandShape::Tapered;
    t.hands.minuteShape   = HandShape::Sword;
    t.hands.secondShape   = HandShape::Needle;
    t.hands.hourLength    = 0.50f;
    t.hands.minuteLength  = 0.78f;
    t.hands.secondLength  = 0.82f;
    t.hands.hourWidth     = 0.10f;
    t.hands.minuteWidth   = 0.065f;
    t.hands.secondWidth   = 0.018f;

    t.ticks.cardinalShape = TickShape::Triangle;
    t.ticks.hourShape     = TickShape::Rect;
    t.ticks.minuteShape   = TickShape::Line;
    t.ticks.cardinalSize  = 1.2f;
    t.ticks.hourSize      = 0.8f;
    t.ticks.minuteSize    = 0.5f;
    t.ticks.luminousDots  = true;

    t.numeralStyle        = NumeralStyle::None;
    t.dialRadiusFraction  = 0.42f;
    t.decorativeRings     = 1;
    t.glowIntensity       = 0.8f;
    t.breatheAmount       = 0.15f;
    return t;
}

inline ClockTheme makeAviatorTheme()
{
    ClockTheme t;
    t.name = "Aviator";
    t.colors.dial           = QColor(35, 35, 35);
    t.colors.rim            = QColor(140, 140, 140);
    t.colors.hourHand       = QColor(230, 230, 230);
    t.colors.minuteHand     = QColor(230, 230, 230);
    t.colors.secondHand     = QColor(255, 140, 30);
    t.colors.ticks          = QColor(210, 210, 210);
    t.colors.cardinalTicks  = QColor(240, 240, 240);
    t.colors.numerals       = QColor(220, 220, 220);
    t.colors.lume           = QColor(200, 200, 180);
    t.colors.centerPin      = QColor(200, 200, 200);
    t.colors.decorativeRing = QColor(100, 100, 100, 80);

    t.hands.hourShape     = HandShape::Sword;
    t.hands.minuteShape   = HandShape::Sword;
    t.hands.secondShape   = HandShape::Needle;
    t.hands.hourLength    = 0.48f;
    t.hands.minuteLength  = 0.76f;
    t.hands.secondLength  = 0.85f;
    t.hands.hourWidth     = 0.09f;
    t.hands.minuteWidth   = 0.06f;
    t.hands.secondWidth   = 0.015f;

    t.ticks.cardinalShape = TickShape::Rect;
    t.ticks.hourShape     = TickShape::Rect;
    t.ticks.minuteShape   = TickShape::Line;
    t.ticks.cardinalSize  = 1.3f;
    t.ticks.hourSize      = 0.9f;
    t.ticks.minuteSize    = 0.4f;

    t.numeralStyle         = NumeralStyle::Arabic;
    t.numeralSizeFraction  = 0.13f;
    t.dialRadiusFraction   = 0.42f;
    t.decorativeRings      = 0;
    t.glowIntensity        = 0.6f;
    t.breatheAmount        = 0.1f;
    return t;
}

inline ClockTheme makeDiverTheme()
{
    ClockTheme t;
    t.name = "Diver";
    t.colors.dial           = QColor(10, 10, 10);
    t.colors.rim            = QColor(120, 120, 120);
    t.colors.hourHand       = QColor(220, 220, 220);
    t.colors.minuteHand     = QColor(220, 220, 220);
    t.colors.secondHand     = QColor(230, 50, 30);
    t.colors.ticks          = QColor(200, 200, 200);
    t.colors.cardinalTicks  = QColor(240, 240, 240);
    t.colors.numerals       = QColor(200, 200, 200);
    t.colors.lume           = QColor(80, 220, 100);
    t.colors.centerPin      = QColor(200, 200, 200);
    t.colors.decorativeRing = QColor(80, 80, 80, 80);

    t.hands.hourShape          = HandShape::Mercedes;
    t.hands.minuteShape        = HandShape::Sword;
    t.hands.secondShape        = HandShape::Needle;
    t.hands.hourLength         = 0.48f;
    t.hands.minuteLength       = 0.76f;
    t.hands.secondLength       = 0.85f;
    t.hands.hourWidth          = 0.10f;
    t.hands.minuteWidth        = 0.06f;
    t.hands.secondWidth        = 0.018f;
    t.hands.secondCounterweight = true;
    t.hands.counterweightLen   = 0.25f;

    t.ticks.cardinalShape = TickShape::Triangle;
    t.ticks.hourShape     = TickShape::Dot;
    t.ticks.minuteShape   = TickShape::None;
    t.ticks.cardinalSize  = 1.2f;
    t.ticks.hourSize      = 1.0f;
    t.ticks.luminousDots  = true;

    t.numeralStyle        = NumeralStyle::None;
    t.dialRadiusFraction  = 0.42f;
    t.decorativeRings     = 2;
    t.glowIntensity       = 0.9f;
    t.breatheAmount       = 0.15f;
    return t;
}

inline ClockTheme makeMinimalistTheme()
{
    ClockTheme t;
    t.name = "Minimalist";
    t.colors.dial           = QColor(5, 5, 5);
    t.colors.rim            = QColor(60, 60, 60);
    t.colors.hourHand       = QColor(200, 200, 200);
    t.colors.minuteHand     = QColor(200, 200, 200);
    t.colors.secondHand     = QColor(180, 180, 180);
    t.colors.ticks          = QColor(140, 140, 140);
    t.colors.cardinalTicks  = QColor(180, 180, 180);
    t.colors.numerals       = QColor(140, 140, 140);
    t.colors.lume           = QColor(100, 100, 100);
    t.colors.centerPin      = QColor(180, 180, 180);
    t.colors.decorativeRing = QColor(40, 40, 40, 60);

    t.hands.hourShape     = HandShape::Needle;
    t.hands.minuteShape   = HandShape::Needle;
    t.hands.secondShape   = HandShape::Needle;
    t.hands.hourLength    = 0.45f;
    t.hands.minuteLength  = 0.72f;
    t.hands.secondLength  = 0.80f;
    t.hands.hourWidth     = 0.05f;
    t.hands.minuteWidth   = 0.035f;
    t.hands.secondWidth   = 0.012f;

    t.ticks.cardinalShape = TickShape::Dot;
    t.ticks.hourShape     = TickShape::Dot;
    t.ticks.minuteShape   = TickShape::None;
    t.ticks.cardinalSize  = 0.9f;
    t.ticks.hourSize      = 0.6f;

    t.numeralStyle        = NumeralStyle::None;
    t.dialRadiusFraction  = 0.40f;
    t.decorativeRings     = 0;
    t.glowIntensity       = 0.3f;
    t.breatheAmount       = 0.3f;
    return t;
}

inline ClockTheme makeChronographTheme()
{
    ClockTheme t;
    t.name = "Chronograph";
    t.colors.dial           = QColor(30, 30, 30);
    t.colors.rim            = QColor(160, 160, 160);
    t.colors.hourHand       = QColor(220, 220, 220);
    t.colors.minuteHand     = QColor(220, 220, 220);
    t.colors.secondHand     = QColor(240, 210, 40);
    t.colors.ticks          = QColor(190, 190, 190);
    t.colors.cardinalTicks  = QColor(230, 230, 230);
    t.colors.numerals       = QColor(200, 200, 200);
    t.colors.lume           = QColor(180, 180, 160);
    t.colors.centerPin      = QColor(220, 220, 220);
    t.colors.decorativeRing = QColor(90, 90, 90, 70);

    t.hands.hourShape          = HandShape::Dauphine;
    t.hands.minuteShape        = HandShape::Dauphine;
    t.hands.secondShape        = HandShape::Needle;
    t.hands.hourLength         = 0.48f;
    t.hands.minuteLength       = 0.75f;
    t.hands.secondLength       = 0.85f;
    t.hands.hourWidth          = 0.09f;
    t.hands.minuteWidth        = 0.06f;
    t.hands.secondWidth        = 0.015f;
    t.hands.secondCounterweight = true;
    t.hands.counterweightLen   = 0.22f;

    t.ticks.cardinalShape = TickShape::Rect;
    t.ticks.hourShape     = TickShape::Rect;
    t.ticks.minuteShape   = TickShape::Line;
    t.ticks.cardinalSize  = 1.1f;
    t.ticks.hourSize      = 0.8f;
    t.ticks.minuteSize    = 0.4f;

    t.numeralStyle        = NumeralStyle::None;
    t.dialRadiusFraction  = 0.42f;
    t.decorativeRings     = 3;
    t.tachymeterRing      = true;
    t.glowIntensity       = 0.7f;
    t.breatheAmount       = 0.1f;
    return t;
}

inline ClockTheme makeNeonRetroTheme()
{
    ClockTheme t;
    t.name = "Neon Retro";
    t.colors.dial           = QColor(5, 5, 15, 180);
    t.colors.rim            = QColor(0, 255, 200);
    t.colors.hourHand       = QColor(0, 255, 200);
    t.colors.minuteHand     = QColor(0, 255, 200);
    t.colors.secondHand     = QColor(255, 80, 200);
    t.colors.ticks          = QColor(0, 255, 200);
    t.colors.cardinalTicks  = QColor(0, 255, 200);
    t.colors.numerals       = QColor(0, 255, 200);
    t.colors.lume           = QColor(0, 255, 200, 100);
    t.colors.centerPin      = QColor(255, 80, 200);
    t.colors.decorativeRing = QColor(0, 255, 200, 40);

    t.hands.hourShape     = HandShape::Alpha;
    t.hands.minuteShape   = HandShape::Alpha;
    t.hands.secondShape   = HandShape::Needle;
    t.hands.hourLength    = 0.48f;
    t.hands.minuteLength  = 0.76f;
    t.hands.secondLength  = 0.85f;
    t.hands.hourWidth     = 0.09f;
    t.hands.minuteWidth   = 0.06f;
    t.hands.secondWidth   = 0.015f;

    t.ticks.cardinalShape = TickShape::Diamond;
    t.ticks.hourShape     = TickShape::Diamond;
    t.ticks.minuteShape   = TickShape::Dot;
    t.ticks.cardinalSize  = 1.3f;
    t.ticks.hourSize      = 0.8f;
    t.ticks.minuteSize    = 0.3f;

    t.numeralStyle         = NumeralStyle::Arabic;
    t.numeralSizeFraction  = 0.14f;
    t.dialRadiusFraction   = 0.42f;
    t.decorativeRings      = 1;
    t.outlineOnly          = true;
    t.hueCycling           = true;
    t.glowIntensity        = 1.5f;
    t.breatheAmount        = 0.2f;
    return t;
}

inline ClockTheme makeDigitalTheme()
{
    ClockTheme t;
    t.name = "Digital";
    t.isDigital = true;
    return t;
}

inline QVector<ClockTheme> getAllClockThemes()
{
    return {
        makeLuxuryTheme(),
        makeAviatorTheme(),
        makeDiverTheme(),
        makeMinimalistTheme(),
        makeChronographTheme(),
        makeNeonRetroTheme(),
        makeDigitalTheme()
    };
}

#endif // CLOCKTHEMES_H
