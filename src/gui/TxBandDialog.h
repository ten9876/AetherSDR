#pragma once

#include "PersistentDialog.h"

namespace AetherSDR {

class RadioModel;

class TxBandDialog : public PersistentDialog {
    Q_OBJECT

public:
    explicit TxBandDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    RadioModel* m_model{nullptr};
};

} // namespace AetherSDR
