#include "outputtable.h"

#include "ast.hpp"
#include <QDropEvent>
#include <QMimeData>

extern AstBuilder builder;

constexpr char OutputTable::mime_type[];

OutputTable::OutputTable(OutputTable::TableType type, QObject *parent) : QAbstractTableModel(parent), m_tableType(type)
{
}

void OutputTable::clear()
{
    removeRows(0, m_modelOutputs.size());
}

QStringList OutputTable::mimeTypes() const
{
    return QStringList(QString(mime_type));
}

const PMMLDocument::DataDictionary & OutputTable::getOutputsMap(bool isNeuron) const
{
    if (isNeuron)
        return builder.context().getNeurons();
    else if (m_tableType == TableType::OutputTable)
        return builder.context().getOutputs();
    else
        return builder.context().getInputs();
}

bool OutputTable::addOutput(const std::string & name, bool isNeuron)
{
    int newIndex = m_modelOutputs.size();
    const auto & outputsMap = getOutputsMap(isNeuron);
    const auto found = outputsMap.find(name);
    if (found != outputsMap.end())
    {
        beginInsertRows(QModelIndex(), newIndex, newIndex);
        m_modelOutputs.emplace_back(name, name, found->second);
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
   return m_modelOutputs.size();
}

int OutputTable::columnCount(const QModelIndex & /*parent*/) const
{
    return m_tableType == TableType::OutputTable ? 5 : 2;
}

QVariant OutputTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (index.row() >= int(m_modelOutputs.size()))
            return QVariant();

        const auto & row = m_modelOutputs[index.row()];
        if (index.column() == 0)
        {
            return QString::fromStdString(row.modelOutput);
        }
        else if (index.column() == 1)
        {
            return QString::fromStdString(row.variableOrAttribute);
        }
        else if (row.field && row.field->field.dataType != PMMLDocument::TYPE_NUMBER)
        {
            // If this isn't a number, forget about these things.
            return QString();
        }
        else if (index.column() == 2)
        {
            return QString::number(row.factor);
        }
        else if (index.column() == 3)
        {
            return QString::number(row.coefficient);
        }
        else if (index.column() == 4)
        {
            if (row.decimalPoints >= 0)
            {
                return QString::number(row.decimalPoints);
            }
            else
            {
                return QVariant();
            }
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
            case 2:
                return QString("Factor");
            case 3:
                return QString("Coefficient");
            case 4:
                return QString("DecimalPoints");
            default:
                return QVariant();
            }
        }
        else
        {
            if (section < int(m_modelOutputs.size()))
            {
                switch(m_modelOutputs[section].field->field.dataType)
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
    Qt::ItemFlags baseFlags = Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | QAbstractTableModel::flags(index);
    if (index.column() == 1)
    {
        baseFlags |= Qt::ItemIsEditable;
    }

    if (index.column() > 1 && checkIndex(index))
    {
        auto & row = m_modelOutputs[index.row()];
        if (row.field == nullptr || row.field->field.dataType == PMMLDocument::TYPE_NUMBER)
        {
            baseFlags |= Qt::ItemIsEditable;
        }
    }

    return baseFlags;
}

Qt::DropActions OutputTable::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

bool OutputTable::moveRows(const QModelIndex &, int srcRow, int count,
                          const QModelIndex &, int dstChild)
{
    beginMoveRows(QModelIndex(), srcRow, srcRow + count - 1, QModelIndex(), dstChild);
    const auto start_iter = m_modelOutputs.begin() + srcRow;
    const auto end_iter = start_iter + count;
    m_modelOutputs.insert(m_modelOutputs.begin() + dstChild, start_iter, end_iter);

    size_t removeIndex = dstChild > srcRow ? srcRow : srcRow+count;
    m_modelOutputs.erase(m_modelOutputs.begin() + removeIndex, m_modelOutputs.begin() + removeIndex + count);

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
            auto & row = m_modelOutputs[index.row()];
            if (index.column() == 1)
            {
                // Changing name back to its default name, or not changing it at all is always acceptable
                if (newName == row.modelOutput || newName == row.variableOrAttribute)
                    return true;

                if (std::any_of(m_modelOutputs.begin(), m_modelOutputs.end(), [&newName](const PMMLExporter::ModelOutput & row)
                                {
                                    return row.variableOrAttribute == newName;
                                }))
                {
                    return false;
                }

                row.variableOrAttribute = newName;
            }
            else if (row.field && row.field->field.dataType != PMMLDocument::TYPE_NUMBER)
            {
                // If this isn't a number, forget about these things.
                return false;
            }
            else if (index.column() == 2)
            {
                bool ok;
                double factor = value.toDouble(&ok);
                if (ok)
                {
                    row.factor = factor;
                }
                return ok;
            }
            else if (index.column() == 3)
            {
                bool ok;
                double coefficient = value.toDouble(&ok);
                if (ok)
                {
                    row.coefficient = coefficient;
                }
                return ok;
            }
            else if (index.column() == 4)
            {
                bool ok;
                int decimalPoints = value.toInt(&ok);
                if (ok)
                {
                    row.decimalPoints = decimalPoints;
                }
                else
                {
                    row.decimalPoints = -1;
                }
                return ok;
            }
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
    std::sort(m_modelOutputs.begin(), m_modelOutputs.end(), [&](const PMMLExporter::ModelOutput & a, const PMMLExporter::ModelOutput & b)
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

        double factor;
        double coefficient;
        int decimalPoints;
        bool isNeuron;
        stream >> outputBinding >> text >> factor >> coefficient >> decimalPoints >> isNeuron;
        const auto & outputsMap = getOutputsMap(isNeuron);

        std::string name = outputBinding.toStdString();
        const auto found = outputsMap.find(name);
        if (found != outputsMap.end())
        {
            newOutputs.emplace_back(name, text.toStdString(), found->second);
            newOutputs.back().factor = factor;
            newOutputs.back().coefficient = coefficient;
            newOutputs.back().decimalPoints = decimalPoints;
        }
    }

    if (!newOutputs.empty())
    {
        beginInsertRows(QModelIndex(), beginRow, beginRow + newOutputs.size() - 1);
        m_modelOutputs.insert(m_modelOutputs.begin() + beginRow, newOutputs.begin(), newOutputs.end());
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

    m_modelOutputs.erase(m_modelOutputs.begin() + row, m_modelOutputs.begin() + row + count);

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
            auto & row = m_modelOutputs[i];
            const bool isNeuron = row.field->origin == PMMLDocument::ORIGIN_TEMPORARY;
            stream << QString::fromStdString(row.modelOutput) << QString::fromStdString(row.variableOrAttribute) << row.factor << row.coefficient << row.decimalPoints << isNeuron;
        }
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setData(mime_type, encodedData);
    return mimeData;
}
