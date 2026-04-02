// src/presets/StockLibrary.h
// Registry of all built-in film stock presets.
// Phase 1: eight stocks with fully traceable published data.
#pragma once

#include "FilmPreset.h"
#include <vector>
#include <string>

namespace MasterFilm {

class StockLibrary {
public:
    static StockLibrary& instance();

    // Returns all registered presets in display order
    const std::vector<FilmPreset>& allPresets() const { return mPresets; }

    // Look up by machine ID — returns nullptr if not found
    const FilmPreset* findById(const std::string& id) const;

    // Filter by category
    std::vector<const FilmPreset*> byCategory(const std::string& category) const;

private:
    StockLibrary();  // populates mPresets

    void registerCinema();  // Kodak Vision3 500T

    std::vector<FilmPreset> mPresets;
};

} // namespace MasterFilm
