#include <iostream>
#include <locale>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <ctime>
#include <fmt/core.h>
#include "raylib.h"
#include "matrix_driver.h"
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

using json = nlohmann::json;

const int texWidth = 64;
const int texHeight = 32;

const int screenZoomFactor = 10;
const int screenWidth = texWidth * screenZoomFactor;
const int screenHeight = texHeight * screenZoomFactor;

uint64_t timeSinceEpochMillisec() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

double seconds_since_local_midnight() {
  time_t now;
  if (time(&now) == -1) {
    return -1;
  }
  struct tm timestamp;
  if (localtime_r(&now, &timestamp) == 0) { // C23
    return -1;
  }
  timestamp.tm_isdst = -1; // Important
  timestamp.tm_hour = 0;
  timestamp.tm_min = 0;
  timestamp.tm_sec = 0;
  time_t midnight = mktime(&timestamp);
  if (midnight == -1) {
    return -1;
  }
  return difftime(now, midnight);
}

void drawOutlinedText(const char* text, int x, int y, int size, Color bg, Color fg) {
    DrawText(text, x-1, y-1, size, bg);
    DrawText(text, x-0, y-1, size, bg);
    DrawText(text, x+1, y-1, size, bg);
    DrawText(text, x-1, y-0, size, bg);
    DrawText(text, x-0, y-0, size, bg);
    DrawText(text, x+1, y-0, size, bg);
    DrawText(text, x-1, y+1, size, bg);
    DrawText(text, x-0, y+1, size, bg);
    DrawText(text, x+1, y+1, size, bg);

    DrawText(text, x, y, size, fg);
}

typedef enum WeatherType
{
    full_sun = 1,
    partial_sun = 2,
    cloudy_sun = 3,
    cloudy_rain = 4,
    cloudy_thunder = 5,
    full_moon = 6,
    partial_moon = 7,
    cloudy_moon = 8
} WeatherType;

int main(int argc, char** argv) {
    InitWindow(screenWidth, screenHeight, "LED Matrix Clock");
    RenderTexture2D target = LoadRenderTexture(texWidth, texHeight);
    MatrixDriver matrixDriver(&argc, &argv, texWidth, texHeight);

    if (matrixDriver.isShim()) {
        SetTargetFPS(30);
    } else {
        SetTargetFPS(1);
    }


    int x = 0;
    int y = 0;
    int velX = 1;
    int velY = 1;

    
    char timeBuffer[256];
    char timeBuffer2[256];
    char dateBuffer[256];

    int temperatures[24];
    int minTemperature = 60;
    int maxTemperature = 80;

    for (int i = 0; i < 24; i++) {
        temperatures[i] = 60;
    }
    uint64_t lastWeatherQuery = 0;
    json jsonWeatherData;

    bool dimMode = false;

    std::string shortForecast;

    // Texture2D dayBg = LoadTextureFromImage(GenImageGradientV(texWidth, texHeight, (Color){0, 0, 0,255}, (Color){43, 169, 252,255}));
    // Texture2D parallaxBgImg = LoadTexture("resources/bg.png");
    WeatherType weatherEnum = WeatherType::full_sun;


    Texture2D weatherIconCloud1 = LoadTexture("resources/weather-icon-cloud-1.png");
    Texture2D weatherIconCloud2 = LoadTexture("resources/weather-icon-cloud-2.png");
    Texture2D weatherIconCloud3 = LoadTexture("resources/weather-icon-cloud-3.png");
    Texture2D weatherIconCloud4 = LoadTexture("resources/weather-icon-cloud-4.png");
    Texture2D weatherIconMoonCloud1 = LoadTexture("resources/weather-icon-moon-cloud-1.png");
    Texture2D weatherIconSun = LoadTexture("resources/weather-icon-sun.png");
    Texture2D weatherIconMoon = LoadTexture("resources/weather-icon-moon.png");

    float timeOfDayPercent = 0.0f;

    // Convert temperature as integer degree F into a table of colors
    Texture2D temperatureScaleImg = LoadTexture("resources/temperature-scale.png");
    Image colorLookupTable = LoadImageFromTexture(temperatureScaleImg);
    Color lookupColors[128];
    for (int i = 0; i < 128; i++) {
        lookupColors[i] = GetImageColor(colorLookupTable, 0, i);
    }

    int secondInDay = seconds_since_local_midnight();
    int sunriseSecondsTime = 5 * 60 * 60;
    int sunsetSecondsTime = 20 * 60 * 60;

    /*
    - ring with sun, moon, sunset, stars, etc as base layer
    
     TODO: weather layers:
     - fog layer
     - rain particle layer
     - lightning particle layer
     - snow particle layer
     - cloud particle layer

     plot of temperature over the next 24 hours
     Dot showing current temperature
     make temp curve darker based on sunset/sunrise
     */

    while (!WindowShouldClose()) {
        // Query weather data if it is expired
        if (timeSinceEpochMillisec() - lastWeatherQuery > 60000) {
            std::cout << "Querying weather API..." << std::endl;
            lastWeatherQuery = timeSinceEpochMillisec();

            // Grab next 24 hours of temperature + weather
            cpr::Response r = cpr::Get(cpr::Url{"https://api.weather.gov/gridpoints/BOX/70,79/forecast/hourly"});
            if (r.status_code == 200) {
                try {
                    json rawPayload = json::parse(r.text);
                    std::cout << jsonWeatherData.dump(4) << std::endl;

                    json periods = rawPayload["properties"]["periods"];

                    for (auto& period : periods) {
                        int hourAfterNow = period["number"];
                        int temperature = period["temperature"];
                        shortForecast = period["shortForecast"];

                        if (hourAfterNow <= 24) {
                            temperatures[hourAfterNow - 1] = temperature;
                        }
                    }

                    std::cout << "FORECAST TEMPS:" << std::endl;
                    for (int i = 0; i < 24; i++) {
                        std::cout << fmt::format("hour {}: {}deg F", i + 1, temperatures[i]) << std::endl;
                    }
                    std::cout << "CURRENT WEATHER" << std::endl;
                    std::cout << shortForecast << std::endl;
                } catch (std::exception &e) {
                    std::cout << "Failed to parse weather API!" << std::endl;
                }
            } else {
                std::cout << "Failed to query weather API!" << std::endl;
            }
        }
        secondInDay = seconds_since_local_midnight();

        // Debug: toggle brightness
        // On real device this is done with the hardware button
        if (IsKeyPressed(32)) {
            dimMode = !dimMode;
        }

        std::time_t now = std::time(nullptr);
        std::strftime(timeBuffer, 256, "%I:%M%p", std::localtime(&now));
        std::strftime(timeBuffer2, 256, "%I:%M", std::localtime(&now));
        std::strftime(dateBuffer, 256, "%b %e", std::localtime(&now));
        // Handle updating clock state!

        timeOfDayPercent = ((timeSinceEpochMillisec() / 100) % 1000) / 1000.0f;
 
        BeginDrawing();

        // Render to internal buffer of same resolution as physical screen
        BeginTextureMode(target);

        // DrawTexture(dayBg, 0, 0, (Color){255,255,255,255});
        ClearBackground((Color){0, 0, 0, 255});

        // dither
        for (int x = -1; x < 20; x++) {
            for (int y = -1; y < 32; y++) {
                if ((x+y) % 2) {
                    DrawPixel(x, y, (Color){255,255,255,128});
                }
            }
        }

        // Draw background parallax tex
        // DrawTexturePro(parallaxBgImg, (Rectangle){ 0, 0, 192,192 }, (Rectangle){32, 90, 192, 192}, (Vector2){96,96}, timeOfDayPercent * 360, WHITE); 

        // Draw time and date
        drawOutlinedText(timeBuffer, 64 - MeasureText(timeBuffer, 5) - 2, 1, 5, (Color){0,0,0,255}, (Color){255,255,255,255});
        drawOutlinedText(dateBuffer, 64 - MeasureText(dateBuffer, 5) - 2, 11, 2, (Color){0,0,0,255}, (Color){255,255,255,255});

        //DrawRectangle(0, 24, 64, 32, (Color){30,30,30,255});
        int temperatureLength = MeasureText(fmt::format("{}", temperatures[0]).c_str(), 2);
        DrawRectangle(2 + temperatureLength, 22, 5,5, (Color){0,0,0,255});
        DrawRectangle(3 + temperatureLength, 23, 3,3, (Color){255,255,255,255});
        DrawRectangle(4 + temperatureLength, 24, 1,1, (Color){0,0,0,255});


        // make everything rendered before this half as bright
        DrawRectangle(0, 0, 64, 32, (Color){0,0,0,128});

        // Draw weather icon
        DrawTexture(weatherIconCloud2, 1, 11, (Color){255,255,255,255});
        
        drawOutlinedText(timeBuffer2, 64 - MeasureText(timeBuffer, 5) - 2, 1, 5, (Color){0,0,0,255}, (Color){255,255,255,255});
        
        // find max and min temperatures
        minTemperature = 999;
        maxTemperature = -999;
        for (int i = 0; i < 24; i++) {
            if (temperatures[i] < minTemperature) {
                minTemperature = temperatures[i];
            }
            if (temperatures[i] > maxTemperature) {
                maxTemperature = temperatures[i];
            }
        }

        DrawRectangle(0, 0, 1, 1, (Color){0,0,255,255});

        // Draw temperature line for the current day
        for (int i = 0; i < 24; i++) {
            int temp_xx = 16 + (i*2);
            int temp = temperatures[i];
            int temp_yy = 31 - (map(temp, minTemperature, maxTemperature, 1, 10));

            Color tempColor = (Color){255,255,255,255};
            if (temp < 0) {
                tempColor = (Color){255,255,255,255};
            } else if (temp >= 128) {
                tempColor = (Color){255,50,50,255};
            } else {
                // Valid lookup
                tempColor = lookupColors[temp];
            }

            float fadePrimaryAmount = 0.25f;
            float fadeSecondaryAmount = 0.6f;

            int secondTime = i * 60 * 60;
            // if (secondTime <= sunriseSecondsTime || secondTime >= sunsetSecondsTime) {
            //     // Nighttime
            //     fadePrimaryAmount = 0.1f;
            //     fadeSecondaryAmount = 0.05f;
            // }

            DrawLine(temp_xx+1, temp_yy, temp_xx+1, 32, Fade(tempColor, fadePrimaryAmount));
            DrawLine(temp_xx+2, temp_yy, temp_xx+2, 32, Fade(tempColor, fadePrimaryAmount));

            DrawPixel(temp_xx, temp_yy,  Fade(tempColor, fadeSecondaryAmount));
            DrawPixel(temp_xx+1, temp_yy,  Fade(tempColor, fadeSecondaryAmount));

        };

        // mask out some edges of temp display
        DrawLine(1,0,1,32, (Color){0,0,0,255});
        DrawLine(0,0,0,32, (Color){0,0,0,255});

        // // Draw an icon indicating the current position in the day since midnight
        // int timeOfDay_idx = map(secondInDay, 0, 86400, 0, 24);
        // int timeOfDay_xx = map(secondInDay, 0, 86400, 0, (24*1.8));

        // if (timeOfDay_idx >= 0 && timeOfDay_idx < 24) {
        //     int timeOfDay_yy = 29 - (map(temperatures[timeOfDay_idx], minTemperature, maxTemperature, 0, 7));

        //     DrawLine(timeOfDay_xx+1, timeOfDay_yy, timeOfDay_xx+1,  30, (Color){255,255,255,200});
        //     //DrawPixel(timeOfDay_xx+1, timeOfDay_yy, (Color){255,255,255,255});
        // }

        // Draw temperature
        drawOutlinedText(fmt::format("{}", temperatures[0]).c_str(), 2, 22, 2, (Color){0,0,0,255}, (Color){255,255,255,255});

        EndTextureMode();

        // Draw a debug UI on the software window
        ClearBackground((Color){0, 0, 0, 255});
        if (dimMode) {
            DrawTexturePro(target.texture, (Rectangle){ 0, 0, texWidth, -texHeight }, (Rectangle){ 0, 0, screenWidth, screenHeight }, (Vector2){0,0}, 0.0f, (Color){255,255,255,100}); 
        } else {
            DrawTexturePro(target.texture, (Rectangle){ 0, 0, texWidth, -texHeight }, (Rectangle){ 0, 0, screenWidth, screenHeight }, (Vector2){0,0}, 0.0f, WHITE); 
        }

        EndDrawing();

        // Grab each pixel in the texture and render to the LED matrix
        // canvas->Clear();
        Image canvasImage = LoadImageFromTexture(target.texture);
        for (int xx = 0; xx < texWidth; xx++) {
            for (int yy = 0; yy < texHeight; yy++) {
                Color pix = GetImageColor(canvasImage, xx, yy);
                matrixDriver.writePixel(xx, texHeight - yy - 1, pix.r, pix.g, pix.b);
            }
        }
        matrixDriver.flipBuffer();
        UnloadImage(canvasImage);
    }

    CloseWindow();
    return 0;
}
