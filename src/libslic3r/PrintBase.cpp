///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv, Roman Beránek @zavorka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Exception.hpp"
#include "PrintBase.hpp"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "I18N.hpp"

namespace Slic3r
{

void PrintTryCancel::operator()() const
{
    m_print->throw_if_canceled();
}

size_t PrintStateBase::g_last_timestamp = 0;

// Update "scale", "input_filename", "input_filename_base" placeholders from the current m_objects.
void PrintBase::update_object_placeholders(DynamicConfig &config, const std::string & /* default_output_ext */) const
{
    // get the first input file name
    std::string input_file;
    std::vector<std::string> v_scale;
    int num_objects = 0;
    int num_instances = 0;
	for (const ModelObject *model_object : m_model.objects) {
		ModelInstance *printable = nullptr;
		for (ModelInstance *model_instance : model_object->instances)
			if (model_instance->is_printable()) {
				printable = model_instance;
				++ num_instances;
			}
		if (printable) {
            ++ num_objects;
	        // CHECK_ME -> Is the following correct ?
			v_scale.push_back("x:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(X) * 100) +
				"% y:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Y) * 100) +
				"% z:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Z) * 100) + "%");
	        if (input_file.empty())
	            input_file = model_object->name.empty() ? model_object->input_file : model_object->name;
	    }
    }
    
    config.set_key_value("num_objects", new ConfigOptionInt(num_objects));
    config.set_key_value("num_instances", new ConfigOptionInt(num_instances));

    config.set_key_value("scale", new ConfigOptionStrings(v_scale));
    if (! input_file.empty()) {
        // get basename with and without suffix
        const std::string input_filename = boost::filesystem::path(input_file).filename().string();
        const std::string input_filename_base = input_filename.substr(0, input_filename.find_last_of("."));
//        config.set_key_value("input_filename", new ConfigOptionString(input_filename_base + default_output_ext));
        config.set_key_value("input_filename_base", new ConfigOptionString(input_filename_base));
    }
}

// Generate an output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters, print statistics - config_override)
std::string PrintBase::output_filename(const std::string &format, const std::string &default_ext, const std::string &filename_base, const DynamicConfig *config_override) const
{
    DynamicConfig cfg;
    if (config_override != nullptr)
    	cfg = *config_override;
    cfg.set_key_value("version", new ConfigOptionString(std::string(SLIC3R_VERSION)));
    PlaceholderParser::update_timestamp(cfg);
    this->update_object_placeholders(cfg, default_ext);
    if (! filename_base.empty()) {
//		cfg.set_key_value("input_filename", new ConfigOptionString(filename_base + default_ext));
		cfg.set_key_value("input_filename_base", new ConfigOptionString(filename_base));
    }
    try {
		boost::filesystem::path filename = format.empty() ?
			cfg.opt_string("input_filename_base") + default_ext :
			this->placeholder_parser().process(format, 0, &cfg);
        if (filename.extension().empty())
            filename.replace_extension(default_ext);
        return filename.string();
    } catch (std::runtime_error &err) {
        throw Slic3r::PlaceholderParserError(_u8L("Failed processing of the output_filename_format template.") + "\n" + err.what());
    }
}

std::string PrintBase::output_filepath(const std::string &path, const std::string &filename_base) const
{
    // if we were supplied no path, generate an automatic one based on our first object's input file
    if (path.empty())
        // get the first input file name
        return (boost::filesystem::path(m_model.propose_export_file_name_and_path()).parent_path() / this->output_filename(filename_base)).make_preferred().string();
    
    // if we were supplied a directory, use it and append our automatically generated filename
    boost::filesystem::path p(path);
    if (boost::filesystem::is_directory(p))
        return (p / this->output_filename(filename_base)).make_preferred().string();
    
    // if we were supplied a file which is not a directory, use it
    return path;
}

void PrintBase::status_update_warnings(int step, PrintStateBase::WarningLevel /* warning_level */, const std::string &message, const PrintObjectBase* print_object)
{
    if (m_status_callback) {
        auto status = print_object ? SlicingStatus(*print_object, step) : SlicingStatus(*this, step);
        m_status_callback(status);
    }
    else if (! message.empty()) {
        printf("%s warning: %s\n",  print_object ? "print_object" : "print", message.c_str());
        std::fflush(stdout);
    }
}

std::mutex& PrintObjectBase::state_mutex(PrintBase *print)
{ 
	return print->state_mutex();
}

std::function<void()> PrintObjectBase::cancel_callback(PrintBase *print)
{ 
	return print->cancel_callback();
}

void PrintObjectBase::status_update_warnings(PrintBase *print, int step, PrintStateBase::WarningLevel warning_level, const std::string &message)
{
    print->status_update_warnings(step, warning_level, message, this);
}

} // namespace Slic3r
