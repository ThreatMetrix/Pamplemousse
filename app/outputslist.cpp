#include "outputslist.h"
#include "outputtable.h"
#include "QMimeData"

OutputsList::OutputsList(QWidget *parent) :
    QListWidget(parent)
{
}

QStringList OutputsList::mimeTypes() const
{
    return QStringList(QString(OutputTable::mime_type));
}

QMimeData *OutputsList::mimeData(const QList<QListWidgetItem *> items) const
{
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);
    for (const QListWidgetItem * item : items)
    {
        PMMLExporter::ModelOutput myoutputter = PMMLExporter::ModelOutput(std::string(), std::string());
        const bool isNeuron = false;
        stream << item->text() << item->text() << myoutputter.factor << myoutputter.coefficient << myoutputter.decimalPoints << isNeuron;
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setData(OutputTable::mime_type, encodedData);
    return mimeData;
}
