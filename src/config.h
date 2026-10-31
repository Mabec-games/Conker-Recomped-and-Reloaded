#pragma once
// settings.toml, read and written whole with toml++ (the game's own library), so keys the launcher does not know
// about are kept as they are. Paths are dotted: "player1.multiplayer.keys".
#include <QString>
#include <QStringList>

#include <toml++/toml.hpp>

class Config {
public:
    bool Load(const QString& file, QString* error = nullptr);  // a missing file is an empty one
    bool Save(QString* error = nullptr) const;
    const QString& File() const { return file_; }

    bool Has(const QString& path) const;
    bool Bool(const QString& path, bool def) const;
    int Int(const QString& path, int def) const;
    QString Str(const QString& path, const QString& def = {}) const;
    // a key binding: one name, a list of names, or "none"; empty when absent
    QStringList Keys(const QString& path, bool* present = nullptr) const;

    void SetBool(const QString& path, bool v);
    void SetInt(const QString& path, int v);
    void SetStr(const QString& path, const QString& v);
    void SetKeys(const QString& path, const QStringList& names);  // as the game writes them

private:
    toml::table* Parent(const QString& path, std::string* leaf);  // made as needed
    toml::node_view<const toml::node> At(const QString& path) const;

    toml::table table_;
    QString file_;
};
