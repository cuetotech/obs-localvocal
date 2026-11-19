#include "model-library-dialog.h"

#include "model-utils/installed-models.h"
#include "model-utils/model-downloader.h"
#include "model-utils/model-downloader-types.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

void open_localvocal_model_library(QWidget *parent, ModelType initial_type)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("LocalVocal - Manage Models");
	dialog.resize(750, 490);
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(
		"Search available transcription models. Only installed models appear "
		"in the main LocalVocal model selector. Downloads require an explicit click.",
		&dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *category = new QComboBox(&dialog);
	category->addItem("Speech recognition", static_cast<int>(MODEL_TYPE_TRANSCRIPTION));
	category->addItem("Local translation", static_cast<int>(MODEL_TYPE_TRANSLATION));
	category->setCurrentIndex(initial_type == MODEL_TYPE_TRANSLATION ? 1 : 0);
	layout->addWidget(category);
	auto *query = new QLineEdit(&dialog);
	query->setPlaceholderText("Search model names, languages, or descriptions...");
	layout->addWidget(query);

	auto *table = new QTableWidget(0, 3, &dialog);
	table->setHorizontalHeaderLabels({"Model", "Language", "Status"});
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(table);

	auto *status = new QLabel(&dialog);
	layout->addWidget(status);
	auto *buttons = new QHBoxLayout();
	auto *download = new QPushButton("Download selected", &dialog);
	auto *refresh = new QPushButton("Refresh installed", &dialog);
	auto *close = new QPushButton("Close", &dialog);
	buttons->addWidget(download);
	buttons->addWidget(refresh);
	buttons->addStretch();
	buttons->addWidget(close);
	layout->addLayout(buttons);

	const auto redraw = [table, category, query, status, download] {
		table->setRowCount(0);
		const auto search = query->text().trimmed();
		int installed = 0, available = 0;
		const auto type = static_cast<ModelType>(category->currentData().toInt());
		for (const auto &model : get_sorted_models_info(type)) {
			if (model.files.empty() || model.local_folder_name.empty())
				continue; // Exclude non-model catalog separators.
			const bool local = localvocal::model_installed_locally(model);
			if (local)
				++installed;
			else
				++available;
			const auto name = QString::fromStdString(model.friendly_name);
			const auto language = QString::fromStdString(model.extra.language);
			const auto description = QString::fromStdString(model.extra.description);
			if (!search.isEmpty() &&
			    !name.contains(search, Qt::CaseInsensitive) &&
			    !language.contains(search, Qt::CaseInsensitive) &&
			    !description.contains(search, Qt::CaseInsensitive))
				continue;
			const int row = table->rowCount();
			table->insertRow(row);
			auto *name_item = new QTableWidgetItem(name);
			name_item->setData(Qt::UserRole, name);
			table->setItem(row, 0, name_item);
			table->setItem(row, 1, new QTableWidgetItem(language));
			table->setItem(row, 2, new QTableWidgetItem(
				local ? "Installed" : "Available to download"));
		}
		status->setText(QString("%1 installed, %2 available in offline catalog. "
				      "Close/reopen Filters to refresh the model selector.")
					.arg(installed)
					.arg(available));
		download->setEnabled(table->rowCount() > 0);
		if (table->rowCount())
			table->selectRow(0);
	};
	QObject::connect(category, QOverload<int>::of(&QComboBox::currentIndexChanged),
			 &dialog, [redraw](int) { redraw(); });
	QObject::connect(query, &QLineEdit::textChanged, &dialog,
			 [redraw](const QString &) { redraw(); });
	QObject::connect(refresh, &QPushButton::clicked, &dialog, redraw);
	QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
	QPointer<QDialog> guard(&dialog);
	QObject::connect(download, &QPushButton::clicked, &dialog,
			 [table, download, redraw, guard, &dialog] {
				 const int row = table->currentRow();
				 if (row < 0 || !table->item(row, 0))
					 return;
				 const auto name =
					 table->item(row, 0)->data(Qt::UserRole).toString().toStdString();
				 const auto &catalog = models_info();
				 const auto it = catalog.find(name);
				 if (it == catalog.end())
					 return;
				 const ModelInfo chosen = it->second;
				 if (localvocal::model_installed_locally(chosen)) {
					 QMessageBox::information(&dialog, "Already installed",
								  "This model is already available to LocalVocal.");
					 return;
				 }
				 download->setEnabled(false);
				 // No automatic download or changing the active filter model.
				 download_model_with_ui_dialog(
					 chosen,
					 [guard, redraw, download](int result, const std::string &) {
						 if (guard.isNull())
							 return;
						 redraw();
						 download->setEnabled(true);
						 if (result != 0)
							 return; // ModelDownloader already reports errors.
					 });
			 });
	redraw();
	dialog.exec();
}
