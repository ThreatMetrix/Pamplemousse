#ifndef OUTPUTTABLE_H
#define OUTPUTTABLE_H

#include <QAbstractTableModel>
#include "modeloutput.hpp"


class ScriptOutputItem;

class OutputTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class TableType
    {
        OutputTable,
        InputTable
    };
    static constexpr char mime_type[] = "application/output_item";
    OutputTable(TableType type, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;
    bool moveRows(const QModelIndex &srcParent, int srcRow, int count, const QModelIndex &dstParent, int dstChild) override;
    void sort(int column, Qt::SortOrder order) override;
    bool canDropMimeData(const QMimeData *, Qt::DropAction, int, int, const QModelIndex &) const override;
    bool removeRows(int row, int count, const QModelIndex &index = QModelIndex()) override;
    bool removeColumns(int column, int count, const QModelIndex &index = QModelIndex()) override;
    Qt::DropActions supportedDropActions() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QStringList mimeTypes() const override;

    bool addOutput(const std::string & name);
    void clear();

private:
    const PMMLDocument::DataDictionary & getOutputsMap() const;
    std::vector<PMMLExporter::ModelOutput> outputs;
    TableType tableType;


signals:

};

#endif // OUTPUTTABLE_H
