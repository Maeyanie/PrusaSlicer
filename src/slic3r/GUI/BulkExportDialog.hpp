///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#pragma once

//#include <wx/gdicmn.h>

#include <boost/filesystem/path.hpp>
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"

class wxString;
class wxStaticText;
class wxTextCtrl;
class wxStaticBitmap;

namespace Slic3r {
class Print;
struct GCodeProcessorResult;

namespace GUI {

class BulkExportDialog : public DPIDialog
{
public:
    enum class ItemStatus { Valid, NoValid, Warning };

    struct Item
    {
        using Validator = std::function<
            std::pair<BulkExportDialog::ItemStatus, wxString>(
                boost::filesystem::path,
                std::string
            )
        >;
        Item(
            wxWindow *parent,
            wxBoxSizer *sizer,
            const boost::filesystem::path &path,
            Validator validator
        );
        Item(const Item &) = delete;
        Item& operator=(const Item &) = delete;
        Item(Item &&) = delete;
        Item& operator=(Item &&) = delete;

        // Item cannot have copy or move constructors, because a wx event binds
        // directly to its address.

        void update_valid_bmp();
        bool is_valid() const { return m_status != ItemStatus::NoValid; }

        boost::filesystem::path path;

    private:
        ItemStatus m_status{ItemStatus::NoValid};
        wxWindow *m_parent{nullptr};
        wxStaticBitmap *m_valid_bmp{nullptr};
        wxTextCtrl *m_text_ctrl{nullptr};
        wxStaticText *m_valid_label{nullptr};
        Validator m_validator;
        boost::filesystem::path m_directory{};

        void init_input_name_ctrl(wxBoxSizer *input_name_sizer, const std::string &path);
        void update();
    };

private:
    // This must be a unique ptr, because Item does not have copy nor move constructors.
    std::vector<std::unique_ptr<Item>> m_items;
    wxBoxSizer *m_sizer{nullptr};

public:
    BulkExportDialog(const std::vector<boost::filesystem::path> &paths);
    bool Layout() override;
    std::vector<boost::filesystem::path> get_paths() const;

protected:
    void on_dpi_changed(const wxRect &) override;
    void on_sys_color_changed() override {}

private:
    void AddItem(const boost::filesystem::path &path);
    bool enable_ok_btn() const;
};

} // namespace GUI
}