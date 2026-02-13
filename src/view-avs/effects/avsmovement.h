#ifndef AVSMOVEMENT_H
#define AVSMOVEMENT_H

#include "avseffect.h"
#include "avsframebuffer.h"
#include <vector>

class AvsMovement : public AvsEffect
{
public:
    enum MovementType { ZoomIn, ZoomOut, Swirl, SwirlOut, Tunnel, SuckIn };

    explicit AvsMovement(MovementType type = ZoomIn);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Movement"; }

    void setMovementType(MovementType type);

private:
    struct DisplaceEntry {
        int srcX;
        int srcY;
    };

    MovementType m_type;
    std::vector<DisplaceEntry> m_displaceTable;
    AvsFramebuffer m_backBuffer;
    int m_width;
    int m_height;

    void buildTable();
};

#endif // AVSMOVEMENT_H
