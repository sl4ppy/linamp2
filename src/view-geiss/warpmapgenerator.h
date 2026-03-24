#ifndef WARPMAPGENERATOR_H
#define WARPMAPGENERATOR_H

#include <QThread>
#include <QMetaType>
#include <vector>
#include "warpparams.h"

Q_DECLARE_METATYPE(std::vector<WarpEntry>)

class WarpMapGenerator : public QThread
{
    Q_OBJECT
public:
    explicit WarpMapGenerator(QObject *parent = nullptr) : QThread(parent) {}

    void generate(const WarpParams &params);

signals:
    void mapReady(std::vector<WarpEntry> newMap);

protected:
    void run() override;

private:
    WarpParams m_params;
};

#endif // WARPMAPGENERATOR_H
