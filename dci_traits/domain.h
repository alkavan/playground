// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#ifndef DOMAIN_H
#define DOMAIN_H

enum class TimeOfDay
{
    Morning,
    Noon,
    Evening,
    Night,
};

enum class Season
{
    Winter,
    Spring,
    Summer,
    Autumn,
};

enum class Location
{
    Roaming,
    Den,
};

enum class Activity
{
    Foraging,
    Hunting,
    Hibernating,
    Scavenging,
    Marking,
    Breeding,
    Fishing,
    Socializing,
    Nurturing,
    Sunning,
};

class Bear {
    std::string name_;
    std::string species_;
    int cubs_;
public:
    Bear(std::string name, std::string species)
    : name_(std::move(name)), species_(std::move(species)), cubs_(0) {}

    [[nodiscard]] std::string getName() const {
        return name_;
    }

    [[nodiscard]] std::string getSpecies() const {
        return species_;
    }

    [[nodiscard]] bool haveCubs() const {
        return cubs_ > 0;
    }

    void breedCub() {
        cubs_++;
    }
};

class Fox {
    std::string name_;
    std::string species_;
    int pups_;
public:
    explicit Fox(std::string name, std::string species)
    : name_(std::move(name)), species_(std::move(species)), pups_(0) {}

    [[nodiscard]] std::string getName() const {
        return name_;
    }

    [[nodiscard]] std::string getSpecies() const {
        return species_;
    }

    [[nodiscard]] bool havePups() const {
        return pups_ > 0;
    }

    void breedPup() {
        pups_++;
    }
};

#endif //DOMAIN_H
