#ifndef OUTPUTSLIST_H
#define OUTPUTSLIST_H

#include <QListWidget>

class OutputsList : public QListWidget
{
    Q_OBJECT
public:
    explicit OutputsList(QWidget *parent = nullptr);
    QStringList mimeTypes() const override;

    QMimeData *mimeData(const QList<QListWidgetItem *> items) const override;
};

#endif // OUTPUTSLIST_H
