#pragma once

#include "model-utils/model-downloader-types.h"
class QWidget;

// Search the shipped offline model catalog. Download is explicitly initiated
// by the operator, and the dialog never switches active OBS sources.
void open_localvocal_model_library(QWidget *parent, ModelType initial_type);
