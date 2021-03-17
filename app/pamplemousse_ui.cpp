//  Copyright 2018-2020 Lexis Nexis Risk Solutions
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
//  Created by Caleb Moore on 2/9/20.
//

#include "pamplemousse_ui.h"
#include "QFileDialog"
#include "QMessageBox"
#include "QProgressDialog"

#include "basicexport.hpp"
#include "document.hpp"
#include "conversioncontext.hpp"
#include "modeloutput.hpp"
#include "luaconverter/luaconverter.hpp"
#include "luaconverter/optimiser.hpp"

#include <fstream>
#include <sstream>

AstBuilder builder;

PamplemousseUI::PamplemousseUI(QWidget *parent) : QMainWindow(parent),
    inputs(OutputTable::TableType::InputTable),
    outputs(OutputTable::TableType::OutputTable)
{
    setupUi(this);
    script_outputs->setModel(&outputs);
    script_outputs->verticalHeader()->setSectionsMovable(true);
    script_outputs->verticalHeader()->setVisible(true);

    connect(
        script_outputs->selectionModel(),
        SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
        SLOT(slotSelectionChanged(const QItemSelection &, const QItemSelection &))
    );

    input_list->setModel(&inputs);
    input_list->verticalHeader()->setSectionsMovable(true);
    input_list->verticalHeader()->setVisible(true);
    clear();
}

void PamplemousseUI::clear()
{
    model_outputs->clear();
    outputs.clear();
    inputs.clear();
    neurons->clear();
    neurons->addItem(tr("Neurons"));

    addButton->setDisabled(true);
    removeButton->setDisabled(true);
    neurons->setDisabled(true);
}

void PamplemousseUI::on_addButton_clicked()
{
    outputs.addOutput(model_outputs->currentItem()->text().toStdString(), false);
}

void PamplemousseUI::on_removeButton_clicked()
{
    const QModelIndexList & selection = script_outputs->selectionModel()->selectedRows();
    for (const auto & row : selection)
    {
        outputs.removeRow(row.row());
    }
}

void PamplemousseUI::on_actionOpen_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this,
            tr("OpenPMMLFile"), "",
            tr("Portable Model Markup Language (*.pmml);;All Files (*)"));

    if (fileName.isEmpty())
       return;

    QProgressDialog progress("Operation in progress.", "Cancel", 0, 3);

    tinyxml2::XMLDocument doc(fileName.toStdString().c_str());
    tinyxml2::XMLError error = tinyxml2::XML_SUCCESS;
    FILE* f;
    {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::information(this, tr("Unable to open file"),
                file.errorString());
            return;
        }

        int fd = file.handle();
        f = fdopen(fd, "rb");
        error = doc.LoadFile(f);
    }
    // This must be closed after file.
    fclose(f);

    if (error != tinyxml2::XML_SUCCESS)
    {
        QMessageBox::information(this, tr("Unable to parse XML"),
            doc.ErrorStr());
        return;
    }

    AstBuilder newBuilder;

    class PopupErrorHook : public AstBuilder::CustomErrorHook
    {
        QMainWindow * m_window;
    public:
        explicit PopupErrorHook(QMainWindow * window) : m_window(window) {}
        void error(const char * msg, int lineNo) const override
        {
            std::stringstream ss;
            ss << msg << "\nAt " << lineNo;
            QMessageBox::information(m_window, tr("Unable to parse pmml"),
                ss.str().c_str());
        }
        void errorWithArg(const char * msg, const char * arg, int lineNo) const override
        {
            std::stringstream ss;
            ss << msg << "(" << arg << ")\nAt " << lineNo;
            QMessageBox::information(m_window, tr("Unable to parse pmml"),
                ss.str().c_str());
        }
    };

    newBuilder.m_customErrorHook = std::make_shared<PopupErrorHook>(this);

    if (!PMMLDocument::convertPMML( newBuilder, doc.RootElement() ))
    {
        return;
    }

    newBuilder.m_customErrorHook.reset();

    importLoadedModel(std::move(newBuilder));
}

void PamplemousseUI::importLoadedModel(AstBuilder && newBuilder)
{
    clear();
    builder = std::move(newBuilder);
    somethingLoaded = true;

    for (const auto & column : builder.context().getInputs())
    {
        inputs.addOutput(column.first, false);
    }

    for (const auto & column : builder.context().getOutputs())
    {
        model_outputs->addItem(QString::fromStdString(column.first));
    }

    if (!builder.context().getNeurons().empty())
    {
        neurons->setDisabled(false);
    }

    for (const auto & column : builder.context().getNeurons())
    {
        neurons->addItem(QString::fromStdString(column.first));
    }
}

void PamplemousseUI::setInsensitive()
{
    actionCase_Insensitive->toggled(true);
}

void PamplemousseUI::setTableIn()
{
    inputMode->setCurrentIndex(1);
}

void PamplemousseUI::setTableOut()
{
    outputMode->setCurrentIndex(1);
}

void PamplemousseUI::on_model_outputs_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (current != nullptr && previous == nullptr)
    {
        addButton->setDisabled(false);
        script_outputs->clearSelection();
    }

    if (current == nullptr && previous != nullptr)
    {
        addButton->setDisabled(true);
    }
}

void PamplemousseUI::slotSelectionChanged(const QItemSelection &, const QItemSelection &)
{
    if (!script_outputs->selectionModel()->selectedRows().empty())
    {
        removeButton->setDisabled(false);
        model_outputs->setCurrentItem(nullptr);
    }
    else
    {
        removeButton->setDisabled(true);
    }
}

void PamplemousseUI::on_actionCase_Insensitive_toggled(bool arg1)
{
    actionCase_Insensitive->setText(arg1 ? tr("Case Insensitive") : tr("Case Sensitive"));
}

void PamplemousseUI::on_actionExport_triggered()
{
    if (!somethingLoaded)
    {
        QMessageBox::information(this, tr("Cannot export"), tr("No model loaded"));
        return;
    }

    if (outputs.modelOutputs().empty())
    {
        QMessageBox::information(this, tr("Cannot export"), tr("No script outputs selected"));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
            tr("Save Lua File"), "",
            tr("Lua source file (*.lua);;All Files (*)"));

    if (fileName.isEmpty())
       return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly))
    {
        QMessageBox::information(this, tr("Unable to open file"),
            file.errorString());
        return;
    }

    std::ofstream out;

    out.__open(file.handle(), std::ios_base::out);

    AstBuilder myBuilder = builder;

    LuaOutputter luaOutputter(out, actionCase_Insensitive->isChecked() ? LuaOutputter::OPTION_LOWERCASE : 0);

    // This is a custom field that is a table containing all other attributes if you are passing them as a table.
    std::vector<PMMLExporter::ModelOutput> tableInput;
    const bool useTableInput = inputMode->currentIndex() == 1;
    if (useTableInput)
    {
        // Put this in front of the model
        AstNode model = myBuilder.popNode();

        auto inputVar = myBuilder.context().createVariable(PMMLDocument::TYPE_TABLE, "input", PMMLDocument::ORIGIN_DATA_DICTIONARY);
        tableInput.emplace_back("input", "input");
        tableInput.back().field = inputVar;

        // Add declarations to build the model's inputs from the fields of the table
        for (const auto & input : inputs.modelOutputs())
        {
            if (auto field = input.field)
            {
                myBuilder.constant(input.variableOrAttribute, PMMLDocument::TYPE_STRING);
                myBuilder.fieldIndirect(inputVar, 1);
                myBuilder.declare(field, AstBuilder::HAS_INITIAL_VALUE);
            }
        }

        myBuilder.pushNode(std::move(model));
    }

    if (outputMode->currentIndex() == 0)
    {
        PMMLExporter::addMultiReturnStatement(myBuilder, outputs.modelOutputs());
    }
    else
    {
        PMMLExporter::addTableReturnStatement(myBuilder, outputs.modelOutputs());
    }

    // Put absolutely everything that's been added into a single block.
    myBuilder.block(myBuilder.stackSize());

    AstNode astTree = myBuilder.popNode();
    PMMLDocument::optimiseAST(astTree, luaOutputter);

    if (useTableInput)
    {
        PMMLExporter::addFunctionHeader(luaOutputter, tableInput);
    }
    else
    {
        PMMLExporter::addFunctionHeader(luaOutputter, inputs.modelOutputs());
    }
    LuaConverter::convertAstToLua(astTree, luaOutputter);
    luaOutputter.endBlock();
}

void PamplemousseUI::on_neurons_currentIndexChanged(int index)
{
    // The first one is just a text thing saying: "Neurons"
    if (index > 0)
    {
        outputs.addOutput(neurons->itemText(index).toStdString(), true);
    }
}

#include "moc_pamplemousse_ui.cpp"
