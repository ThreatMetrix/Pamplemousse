#include "outputtable.h"

#include "ast.hpp"
#include <QDropEvent>
#include <QMimeData>

extern AstBuilder builder;

constexpr char OutputTable::mime_type[];

OutputTable::OutputTable(OutputTable::TableType type, QObject *parent) : QAbstractTableModel(parent), tableType(type)
{
}

void OutputTable::clear()
{
    removeRows(0, outputs.size());
}

QStringList OutputTable::mimeTypes() const
{
    return QStringList(QString(mime_type));
}

const PMMLDocument::DataDictionary & OutputTable::getOutputsMap() const
{
    if (tableType == TableType::OutputTable)
        return builder.context().getOutputs();
    else
        return builder.context().getInputs();
}

bool OutputTable::addOutput(const std::string & name)
{
    int newIndex = outputs.size();
    const auto & outputsMap = getOutputsMap();
    const auto found = outputsMap.find(name);
    if (found != outputsMap.end())
    {
        beginInsertRows(QModelIndex(), newIndex, newIndex);
        outputs.emplace_back(name, found->second->luaName, found->second);
        endInsertRows();
        return true;
    }
    else
    {
        return false;
    }
}

int OutputTable::rowCount(const QModelIndex & /*parent*/) const
{
   return outputs.size();
}

int OutputTable::columnCount(const QModelIndex & /*parent*/) const
{
    return 2;
}

QVariant OutputTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (index.row() >= int(outputs.size()))
            return QVariant();

        const auto & row = outputs[index.row()];
        if (index.column() == 0)
        {
            return QString::fromStdString(row.modelOutput);
        }
        else if (index.column() == 1)
        {
            return QString::fromStdString(row.variableOrAttribute);
        }
        else
        {
            return QVariant();
        }
    }

    return QVariant();
}

QVariant OutputTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (orientation == Qt::Horizontal)
        {
            switch (section)
            {
            case 0:
                return QString("Model Output");
            case 1:
                return QString("Variable");
            default:
                return QVariant();
            }
        }
        else
        {
            if (section < int(outputs.size()))
            {
                switch(outputs[section].field->field.dataType)
                {
                case PMMLDocument::TYPE_STRING:
                    return QString("String");

                case PMMLDocument::TYPE_NUMBER:
                    return QString("Number");

                case PMMLDocument::TYPE_BOOL:
                    return QString("Bool");

                case PMMLDocument::TYPE_INVALID:
                case PMMLDocument::TYPE_VOID:
                case PMMLDocument::TYPE_LAMBDA:
                case PMMLDocument::TYPE_TABLE:
                case PMMLDocument::TYPE_STRING_TABLE:
                    return QString("Other");
                }
            }

            return QVariant();
        }
    }
    return QVariant();
}

Qt::ItemFlags OutputTable::flags(const QModelIndex &index) const
{
    if (index.column() == 1)
    {
        return Qt::ItemIsEditable | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled | QAbstractTableModel::flags(index);
    }
    else
    {
        return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | QAbstractTableModel::flags(index);
    }
}


Qt::DropActions OutputTable::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}


bool OutputTable::moveRows(const QModelIndex &, int srcRow, int count,
                          const QModelIndex &, int dstChild)
{
    beginMoveRows(QModelIndex(), srcRow, srcRow + count - 1, QModelIndex(), dstChild);
    const auto start_iter = outputs.begin() + srcRow;
    const auto end_iter = start_iter + count;
    outputs.insert(outputs.begin() + dstChild, start_iter, end_iter);

    size_t removeIndex = dstChild > srcRow ? srcRow : srcRow+count;
    outputs.erase(outputs.begin() + removeIndex, outputs.begin() + removeIndex + count);

    endMoveRows();
    return true;
}

bool OutputTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!checkIndex(index))
        return false;

    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        std::string newName = value.toString().toStdString();
        if (value.isValid())
        {
            auto & row = outputs[index.row()];

            // Changing name back to its default name, or not changing it at all is always acceptable
            if (newName == row.field->luaName || newName == row.variableOrAttribute)
                return true;

            if (builder.context().hasVariableNamed(newName))
                return false;

            // Contains bad characters
            if (newName.empty() || isnumber(newName.front()) ||
                    std::find_if(newName.begin(), newName.end(), [](char a){return !isalnum(a) && a != '_';}) != newName.end())
                return false;

            for (const auto & row : outputs)
            {
                if (row.variableOrAttribute == newName)
                    return false;
            }

            row.variableOrAttribute = newName;
        }
        else if (index.column() == 0)
        {
            removeRows(index.row(), 1);
        }
        return true;
    }
    return false;

}

void OutputTable::sort(int column, Qt::SortOrder order)
{
    std::sort(outputs.begin(), outputs.end(), [&](const PMMLExporter::ModelOutput & a, const PMMLExporter::ModelOutput & b)
    {
        const std::string acmp = column == 0 ? a.modelOutput : a.variableOrAttribute;
        const std::string bcmp = column == 0 ? b.modelOutput : b.variableOrAttribute;
        int cmped = acmp.compare(bcmp);
        if (order == Qt::AscendingOrder)
            cmped *= -1;
        return cmped < 0;
    });

    QModelIndex topLeft = createIndex(0,0);
    QModelIndex bottomRight = createIndex(rowCount(topLeft), columnCount(topLeft));

    emit dataChanged(topLeft, bottomRight, {Qt::DisplayRole});
}

bool OutputTable::canDropMimeData(const QMimeData *, Qt::DropAction, int, int, const QModelIndex &) const
{
    return true;
}

bool OutputTable::dropMimeData(const QMimeData *data, Qt::DropAction, int row, int, const QModelIndex & parent)
{
    int beginRow;
    if (row != -1)
        beginRow = row;
    else if (parent.isValid())
        beginRow = parent.row();
    else
        beginRow = rowCount(QModelIndex());

    QByteArray encodedData = data->data(mime_type);
    QDataStream stream(&encodedData, QIODevice::ReadOnly);

    std::vector<PMMLExporter::ModelOutput> newOutputs;
    while (!stream.atEnd())
    {
        QString outputBinding, text;
        stream >> outputBinding >> text;
        const auto & outputsMap = getOutputsMap();

        std::string name = outputBinding.toStdString();
        const auto found = outputsMap.find(name);
        if (found != outputsMap.end())
        {
            newOutputs.emplace_back(name, text.toStdString(), found->second);
        }
    }

    if (!newOutputs.empty())
    {
        beginInsertRows(QModelIndex(), beginRow, beginRow + newOutputs.size() - 1);
        outputs.insert(outputs.begin() + beginRow, newOutputs.begin(), newOutputs.end());
        endInsertRows();
        return true;
    }
    else
    {
        return false;
    }
}

bool OutputTable::removeRows(int row, int count, const QModelIndex &)
{
    beginRemoveRows(QModelIndex(), row, row + count - 1);

    outputs.erase(outputs.begin() + row, outputs.begin() + row + count);

    endRemoveRows();
    return true;
}

bool OutputTable::removeColumns(int, int, const QModelIndex&)
{
    return false;
}

QMimeData *OutputTable::mimeData(const QModelIndexList &indexes) const
{
    QByteArray encodedData;
    if (!indexes.empty())
    {
        int start = indexes.at(0).row();
        int end = start;
        for (const QModelIndex & index : indexes)
        {
            if (index.row() > end) end = index.row();
            if (index.row() < start) start = index.row();
        }

        QDataStream stream(&encodedData, QIODevice::WriteOnly);
        for (int i = start; i <= end; ++i)
        {
            auto & row = outputs[i];
            stream << QString::fromStdString(row.modelOutput) << QString::fromStdString(row.variableOrAttribute);
        }
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setData(mime_type, encodedData);
    return mimeData;
}
