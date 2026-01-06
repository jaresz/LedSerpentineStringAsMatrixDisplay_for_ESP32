#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>

#define BUTTON_PIN 9
#define AHT_SCL_PIN 5 // GPIO05 - I2C SCL
#define AHT_SDA_PIN 6 // GPIO06 - I2C SDA
// static const uint8_t RGB_BUILTIN = 8;
int buttonState = HIGH;
int lastButtonState = HIGH;
int ledStripPin = 2; // GPIO22 for LED strip data

// Matrix configuration (compile-time constants for memory optimization)
// Change these values to customize your matrix size
const int matrixCols = 32;							   // Columns: 20-40 recommended
const int matrixRows = 7;							   // Rows: 5-12 recommended
const int ledStripNumpixels = matrixCols * matrixRows; // Total LEDs

// Validation constants (for documentation)
const int MIN_COLS = 20;
const int MAX_COLS = 40;
const int MIN_ROWS = 5;
const int MAX_ROWS = 12;
const int MAX_LED_COUNT = 256;

static unsigned long lastLedUpdate = 0;
uint32_t last_ota_time = 0;

// Button timing for long press detection
unsigned long buttonPressStartTime = 0;
bool buttonPressed = false;
const unsigned long LONG_PRESS_DURATION = 5000; // 5 seconds for WiFi reconfiguration

// WiFi configuration variables
bool wifiConfigMode = false;
bool wifiConnected = false;
String savedSSID = "";
String savedPassword = "";
String savedOTAPassword = "";
const char *DEFAULT_OTA_PASSWORD = "OTA_default_pass1";
Preferences preferences;
WebServer server(80);
DNSServer dnsServer;

// Access Point settings for configuration
const char *AP_SSID = "LuminousESP-S3-Setup";
const char *AP_PASSWORD = "bazinga123";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

int selectedPixelNumber = 0;
int pirs = 0;
int lights = 0;
int alarms = 0;

unsigned long lastDebounceTime = 0; // the last time the output pin was toggled
unsigned long debounceDelay = 50;	// the debounce time; increase if the output flickers
unsigned long lastPir1Time = 0;
unsigned long lastMakeLight = 0;

// Temperature sensor
Adafruit_AHTX0 aht;
bool ahtSensorAvailable = false;

// NeoPixel type enumeration and current selection
typedef enum {
	NEOPIXEL_GRB = 0,  // NEO_GRB + NEO_KHZ800
	NEOPIXEL_BGR = 1,  // NEO_BGR + NEO_KHZ800
	NEOPIXEL_RGB = 2,  // NEO_RGB + NEO_KHZ800
	NEOPIXEL_WRGB = 3  // NEO_WRGB + NEO_KHZ800
} NeoPixelType;

// Current pixel type configuration
NeoPixelType currentNeoPixelType = NEOPIXEL_GRB; // Default: NEO_GRB + NEO_KHZ800
uint16_t currentPixelConfig = NEO_GRB + NEO_KHZ800; // Current configuration value

// LED strip object (will be re-initialized with correct type)
Adafruit_NeoPixel myLedStrip(ledStripNumpixels, ledStripPin, NEO_GRB + NEO_KHZ800);
// Note: NEO_GRB + NEO_KHZ800 is the most common configuration for WS2812 LEDs

// Animation speed multiplier (1.0 = normal, 0.5 = half speed, 2.0 = double speed)
float animationSpeed = 1.0;

// Color picker and drawing variables
uint32_t selectedColor = 0x00FF00;			  // Default to green
uint32_t drawingGrid[matrixCols][matrixRows]; // Exact matrix size for pixel colors (0 = off, >0 = color)
bool needsGridUpdate = true;				  // Flag to update grid display

// Global temperature variable for display
float currentTemperature = -999.0;
float currentHumidity = -999.0;
unsigned long lastTemperatureReadTime = 0;

unsigned int previousUploadProgressPercent = 0;

uint8_t activePixel = 0;

// Animation system variables
typedef enum
{
	ANIMATION_ROTATING_HEXAGONS = 0,
	ANIMATION_MATRIX_RAIN = 1,
	ANIMATION_POLISH_FLAG = 2,
	ANIMATION_UKRAINIAN_FLAG = 3,
	ANIMATION_WINDMILL = 4,
	ANIMATION_PACMAN = 5,
	ANIMATION_SNOW = 6,
	ANIMATION_OCEAN = 7,
	ANIMATION_CHAMPAGNE_FIREWORKS = 8,
	ANIMATION_TEMPERATURE = 9,
	ANIMATION_QIX = 10
} AnimationMode;

// Animation state variables
AnimationMode currentAnimation = ANIMATION_ROTATING_HEXAGONS;
unsigned long lastAnimationChange = 0;
const unsigned long ANIMATION_DURATION = 30000; // 30 seconds per animation

// Matrix Digital Rain animation variables
struct RainDrop {
	float y;              // Current Y position
	float speed;          // Fall speed
	uint8_t brightness;   // Character brightness
	uint8_t trailLength;  // Length of trail behind character
	bool active;          // Whether this drop is active
	unsigned long lastUpdate; // Last update time for this drop
};

const int MAX_RAIN_COLUMNS = matrixCols; // One stream per column
RainDrop rainDrops[MAX_RAIN_COLUMNS];
unsigned long lastMatrixRainUpdate = 0;
float matrixGlitchPhase = 0.0;

// Flag animation variables
float flagWavePhase = 0.0;
float flagWaveSpeed = 0.15;
unsigned long lastFlagUpdate = 0;
const int FLAG_WIDTH = (int)(matrixCols * 0.75);  // 75% of matrix width
const int FLAG_HEIGHT = (int)(matrixRows * 0.6);  // 60% of matrix height
const int FLAG_START_X = (matrixCols - FLAG_WIDTH) / 2;  // Center horizontally
const int FLAG_START_Y = (matrixRows - FLAG_HEIGHT) / 2; // Center vertically

// Windmill animation variables
struct WindmillBlade {
	float angle;          // Current angle of the blade
	float length;         // Length of the blade
	uint8_t r, g, b;      // Blade color
};

struct Windmill {
	float centerX, centerY;     // Center position of windmill
	WindmillBlade blades[4];    // Four blades
	float rotationSpeed;        // Speed of rotation
	float currentRotation;      // Current rotation angle
	bool active;
};

Windmill windmill;
unsigned long lastWindmillUpdate = 0;
float sunPhase = 0.0;  // For sun animation
float skyBrightness = 1.0;

// Pac-Man animation variables
struct PacManDot {
	int x, y;
	bool active;
	uint8_t brightness; // For blinking effect
};

struct PacMan {
	float x, y;
	int direction; // 0=right, 1=down, 2=left, 3=up
	bool mouthOpen;
	unsigned long lastMouthToggle;
	unsigned long lastMove;
	int targetX, targetY; // Next position to move to
	bool moving;
	int dotsEaten;
};

const int MAX_DOTS = 20;
PacManDot pacmanDots[MAX_DOTS];
PacMan pacman;
unsigned long lastPacmanUpdate = 0;

// Snow animation variables
struct Snowflake
{
	float x, y;
	float speed;
	uint8_t brightness;
};
const int MAX_SNOWFLAKES = 15;
Snowflake snowflakes[MAX_SNOWFLAKES];
unsigned long lastSnowUpdate = 0;

// Ocean animation variables
float wavePhase = 0.0;
float foamOffset = 0.0;
unsigned long lastOceanUpdate = 0;

// Fireworks animation variables
struct Particle {
	float x, y;
	float vx, vy; // velocity
	uint8_t life; // particle life remaining (0-255)
	uint8_t r, g, b; // color
	uint8_t trailLength; // length of visible trail for chrysanthemum
	bool isSpider; // true for spider type, false for chrysanthemum
	float gravity; // gravity effect (different for each type)
};
const int MAX_PARTICLES = 40;
Particle particles[MAX_PARTICLES];
unsigned long lastFireworkLaunch = 0;
unsigned long lastFireworkUpdate = 0;
const unsigned long FIREWORK_INTERVAL = 2500; // Launch new firework every 2.5 seconds

// Rotating Hexagons animation variables
struct Hexagon {
	float centerX, centerY;
	float rotation; // Current rotation angle in radians
	float rotationSpeed; // Rotation speed (radians per frame)
	float size; // Hexagon size (radius)
	uint8_t r, g, b; // Color
	bool active;
};
const int MAX_HEXAGONS = 6;
Hexagon hexagons[MAX_HEXAGONS];
unsigned long lastHexagonUpdate = 0;
float globalRotation = 0.0;
float colorPhase = 0.0;

// Qix animation variables
struct QixSegment {
	float x, y;          // Position of this segment
	uint8_t r, g, b;     // Color of this segment
};

struct QixObject {
	float headX, headY;      // Head position
	float vx, vy;            // Head velocity
	QixSegment segments[12]; // Connected segments following the head
	uint8_t segmentCount;    // Number of active segments
	bool active;             // Whether object is active
	float segmentSpacing;    // Distance between segments
	unsigned long lastColorChange; // Time of last color shift
};

QixObject qixObject;
unsigned long lastQixUpdate = 0;
float qixSpeedMultiplier = 1.0;

// Temperature display animation variables
unsigned long lastTempUpdate = 0;
float displayedTemperature = -999.0;

// 5x7 digit patterns (each digit is 4 cols wide)
const uint8_t digitPatterns[13][7][4] = {
	// 0
	{{1,1,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,1,1,0}},
	// 1 
	{{0,1,0,0},
	 {1,1,0,0},
	 {0,1,0,0},
	 {0,1,0,0},
	 {0,1,0,0},
	 {0,1,0,0},
	 {0,1,0,0}},
	// 2
	{{1,1,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {1,1,1,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,1,1,0}},
	// 3
	{{1,1,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {1,1,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {1,1,1,0}},
	// 4
	{{1,0,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,1,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {0,0,1,0}},
	// 5
	{{1,1,1,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,1,0,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {1,1,0,0}},
	// 6
	{{1,1,1,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,1,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,1,1,0}},
	// 7
	{{1,1,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {0,0,1,0}},
	// 8
	{{1,1,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,1,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,1,1,0}},
	// 9
	{{1,1,1,0},
	 {1,0,1,0},
	 {1,0,1,0},
	 {1,1,1,0},
	 {0,0,1,0},
	 {0,0,1,0},
	 {1,1,1,0}},
	// . (decimal point)
	{{0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {1,0,0,0}},
	// ° (degree symbol)
	{{1,1,0,0},
	 {1,1,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0},
	 {0,0,0,0}},
	// C
	{{1,1,1,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,0,0,0},
	 {1,1,1,0}}
};

// map column,row -> pixel index for a configurable matrix laid out in a serpentine (zig-zag) strip
int pixelIndex(int col, int row)
{
	// bounds safety
	if (col < 0)
		col = 0;
	if (col >= matrixCols)
		col = matrixCols - 1;
	if (row < 0)
		row = 0;
	if (row >= matrixRows)
		row = matrixRows - 1;

	int base = row * matrixCols;
	if ((row & 1) == 0)
	{
		// even rows go left->right
		return base + col;
	}
	else
	{
		// odd rows go right->left (serpentine)
		return base + (matrixCols - 1 - col);
	}
}

// Initialize Matrix Digital Rain animation
void initMatrixRainAnimation() {
	// Initialize rain drops for each column
	for (int i = 0; i < MAX_RAIN_COLUMNS; i++) {
		rainDrops[i].active = (random(100) < 60); // 60% chance to start active
		rainDrops[i].y = random(-matrixRows * 2, 0); // Start above screen
		rainDrops[i].speed = random(15, 40) / 100.0; // Varying fall speeds
		rainDrops[i].brightness = random(150, 255); // Varying brightness
		rainDrops[i].trailLength = random(3, 8); // Varying trail lengths
		rainDrops[i].lastUpdate = millis() + random(0, 500); // Stagger updates
	}
	
	matrixGlitchPhase = 0.0;
}

// Initialize Windmill animation
void initWindmillAnimation() {
	windmill.active = true;
	
	// Position windmill center off-center for better blade visibility
	windmill.centerX = matrixCols * 0.25; // 1/4 from left edge
	windmill.centerY = matrixRows * 0.6;  // Slightly below center
	
	windmill.rotationSpeed = 0.12; // Moderate rotation speed
	windmill.currentRotation = 0.0;
	
	// Initialize 4 windmill blades
	for (int i = 0; i < 4; i++) {
		windmill.blades[i].angle = i * (PI / 2.0); // 90 degrees apart
		windmill.blades[i].length = max(matrixCols, matrixRows) * 0.8; // Long blades
		
		// Blade colors - white/light gray for traditional windmill look
		windmill.blades[i].r = 245;
		windmill.blades[i].g = 245;
		windmill.blades[i].b = 255; // Slightly bluish white
	}
	
	sunPhase = 0.0;
	skyBrightness = 1.0;
}

// Initialize Pac-Man animation
void initPacmanAnimation() {
	// Initialize Pac-Man
	pacman.x = 1.0;
	pacman.y = matrixRows / 2;
	pacman.direction = 0; // Start moving right
	pacman.mouthOpen = true;
	pacman.lastMouthToggle = millis();
	pacman.lastMove = millis();
	pacman.targetX = 2;
	pacman.targetY = pacman.y;
	pacman.moving = false;
	pacman.dotsEaten = 0;

	// Initialize dots randomly across the matrix
	for (int i = 0; i < MAX_DOTS; i++) {
		pacmanDots[i].active = true;
		pacmanDots[i].brightness = 255;
		
		// Place dots randomly but not too close to Pac-Man's starting position
		do {
			pacmanDots[i].x = random(0, matrixCols);
			pacmanDots[i].y = random(0, matrixRows);
		} while (abs(pacmanDots[i].x - (int)pacman.x) < 3 && abs(pacmanDots[i].y - (int)pacman.y) < 2);
	}
}

// Initialize snow animation
void initSnowAnimation()
{
	for (int i = 0; i < MAX_SNOWFLAKES; i++)
	{
		snowflakes[i].x = random(0, matrixCols * 100) / 100.0; // Sub-pixel precision
		snowflakes[i].y = random(-matrixRows * 2, 0);		   // Start above screen
		snowflakes[i].speed = random(20, 60) / 100.0;		   // Slow falling speed
		snowflakes[i].brightness = random(100, 255);
	}
}

// Generic waving flag animation function
void drawWavingFlag(uint8_t topR, uint8_t topG, uint8_t topB, uint8_t bottomR, uint8_t bottomG, uint8_t bottomB) {
	if (millis() - lastFlagUpdate < 100) return; // Update every 100ms
	lastFlagUpdate = millis();

	myLedStrip.clear();
	
	// Update wave phase
	flagWavePhase += flagWaveSpeed;
	if (flagWavePhase > 6.28) flagWavePhase -= 6.28; // 2*PI

	// Draw the waving flag
	for (int x = 0; x < FLAG_WIDTH; x++) {
		for (int y = 0; y < FLAG_HEIGHT; y++) {
			// Calculate wave displacement for this position
			float waveOffset1 = sin(flagWavePhase + x * 0.3) * 0.8;
			float waveOffset2 = sin(flagWavePhase * 1.3 + x * 0.2 + y * 0.1) * 0.4;
			float totalWaveOffset = waveOffset1 + waveOffset2;
			
			// Apply wave displacement
			int pixelX = FLAG_START_X + x;
			int pixelY = FLAG_START_Y + y + (int)totalWaveOffset;
			
			// Boundary check
			if (pixelX >= 0 && pixelX < matrixCols && pixelY >= 0 && pixelY < matrixRows) {
				int pixelIdx = pixelIndex(pixelX, pixelY);
				
				// Determine which stripe this pixel belongs to
				float stripePosition = (float)y / FLAG_HEIGHT;
				
				uint8_t r, g, b;
				if (stripePosition < 0.5) {
					// Top stripe
					r = topR;
					g = topG;
					b = topB;
				} else {
					// Bottom stripe
					r = bottomR;
					g = bottomG;
					b = bottomB;
				}
				
				// Add slight brightness variation for wave effect
				float brightnessVariation = 1.0 + 0.2 * sin(flagWavePhase * 2 + x * 0.4);
				brightnessVariation = constrain(brightnessVariation, 0.8, 1.2);
				
				r = (uint8_t)(r * brightnessVariation);
				g = (uint8_t)(g * brightnessVariation);
				b = (uint8_t)(b * brightnessVariation);
				
				myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(r, g, b));
			}
		}
	}

	// Add flag pole (simple line on the left)
	if (FLAG_START_X > 0) {
		for (int y = 0; y < matrixRows; y++) {
			int polePixelIdx = pixelIndex(FLAG_START_X - 1, y);
			myLedStrip.setPixelColor(polePixelIdx, myLedStrip.Color(139, 69, 19)); // Brown pole
		}
	}

	myLedStrip.show();
}

// Polish flag animation (white top, red bottom)
void updatePolishFlagAnimation() {
	drawWavingFlag(255, 255, 255,  // Top stripe: white
	               255, 0, 0);     // Bottom stripe: red
}

// Ukrainian flag animation (blue top, yellow bottom)
void updateUkrainianFlagAnimation() {
	drawWavingFlag(0, 87, 183,     // Top stripe: blue
	               255, 215, 0);   // Bottom stripe: yellow
}

// Initialize flag animations
void initFlagAnimations() {
	flagWavePhase = 0.0;
	flagWaveSpeed = 0.15;
}

// Matrix Digital Rain animation
void updateMatrixRainAnimation() {
	if (millis() - lastMatrixRainUpdate < 80) return; // Update every 80ms
	lastMatrixRainUpdate = millis();

	myLedStrip.clear();

	// Update glitch phase for occasional flickers
	matrixGlitchPhase += 0.1;
	if (matrixGlitchPhase > 6.28) matrixGlitchPhase -= 6.28; // 2*PI

	// Update each rain column
	for (int col = 0; col < MAX_RAIN_COLUMNS; col++) {
		if (!rainDrops[col].active) {
			// Randomly reactivate inactive drops
			if (random(1000) < 15) { // 1.5% chance per update
				rainDrops[col].active = true;
				rainDrops[col].y = random(-matrixRows, -1); // Start above screen
				rainDrops[col].speed = random(15, 40) / 100.0;
				rainDrops[col].brightness = random(150, 255);
				rainDrops[col].trailLength = random(3, 8);
				rainDrops[col].lastUpdate = millis();
			}
			continue;
		}

		// Update position
		if (millis() - rainDrops[col].lastUpdate > random(50, 150)) {
			rainDrops[col].y += rainDrops[col].speed;
			rainDrops[col].lastUpdate = millis();
		}

		// Reset if drop has fallen off screen
		if (rainDrops[col].y > matrixRows + rainDrops[col].trailLength) {
			rainDrops[col].active = false;
			continue;
		}

		// Draw the rain trail
		for (int trail = 0; trail < rainDrops[col].trailLength; trail++) {
			int row = (int)(rainDrops[col].y - trail);
			
			if (row >= 0 && row < matrixRows) {
				int pixelIdx = pixelIndex(col, row);
				
				// Calculate fade factor for trail
				float fadeFactor = 1.0 - (float)trail / rainDrops[col].trailLength;
				fadeFactor = fadeFactor * fadeFactor; // Quadratic fade
				
				// Leading character is brightest (white-green)
				uint8_t green, red, blue;
				if (trail == 0) {
					// Head of trail - bright white-green
					red = (uint8_t)(rainDrops[col].brightness * 0.4 * fadeFactor);
					green = (uint8_t)(rainDrops[col].brightness * fadeFactor);
					blue = (uint8_t)(rainDrops[col].brightness * 0.4 * fadeFactor);
				} else {
					// Trail - pure green with fade
					red = 0;
					green = (uint8_t)(rainDrops[col].brightness * fadeFactor * 0.8);
					blue = 0;
				}

				// Add occasional glitch effect
				if ((int)(matrixGlitchPhase * 10) % 30 == 0 && random(100) < 5) {
					red = constrain(red + 100, 0, 255);
					blue = constrain(blue + 50, 0, 255);
				}

				// Ensure we don't overwrite brighter pixels
				uint32_t existingColor = myLedStrip.getPixelColor(pixelIdx);
				uint8_t existingG = (existingColor >> 8) & 0xFF;
				
				if (green > existingG) {
					myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(red, green, blue));
				}
			}
		}

		// Occasionally change speed for dynamic effect
		if (random(1000) < 5) {
			rainDrops[col].speed = random(15, 40) / 100.0;
		}
	}

	// Add occasional horizontal glitch lines
	if ((int)(matrixGlitchPhase * 20) % 100 == 0 && random(100) < 8) {
		int glitchRow = random(0, matrixRows);
		for (int col = 0; col < matrixCols; col++) {
			int pixelIdx = pixelIndex(col, glitchRow);
			if (random(100) < 40) { // 40% of pixels in glitch line
				uint8_t glitchBrightness = random(100, 200);
				myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(
					0, glitchBrightness, 0)); // Pure green glitch
			}
		}
	}

	// Add random bright flashes (like in the movie)
	if (random(1000) < 3) {
		int flashCol = random(0, matrixCols);
		int flashRow = random(0, matrixRows);
		int flashPixelIdx = pixelIndex(flashCol, flashRow);
		myLedStrip.setPixelColor(flashPixelIdx, myLedStrip.Color(200, 255, 200)); // Bright white-green flash
	}

	myLedStrip.show();
}

// Windmill animation with rotating blades and sunny sky
void updateWindmillAnimation() {
	if (millis() - lastWindmillUpdate < 50) return; // Update every 50ms for smooth rotation
	lastWindmillUpdate = millis();

	myLedStrip.clear();

	// Update sun and sky animation
	sunPhase += 0.02;
	if (sunPhase > 6.28) sunPhase -= 6.28; // 2*PI
	
	skyBrightness = 0.8 + 0.2 * sin(sunPhase * 0.3); // Gentle sky brightness variation

	// Draw animated sun in corner
	float sunX = matrixCols * 0.85 + 2 * sin(sunPhase);
	float sunY = matrixRows * 0.15 + 1 * cos(sunPhase * 0.7);
	
	int sunCol = (int)round(sunX);
	int sunRow = (int)round(sunY);
	
	if (sunCol >= 0 && sunCol < matrixCols && sunRow >= 0 && sunRow < matrixRows) {
		int sunPixelIdx = pixelIndex(sunCol, sunRow);
		uint8_t sunBrightness = (uint8_t)(200 + 55 * sin(sunPhase * 2));
		myLedStrip.setPixelColor(sunPixelIdx, myLedStrip.Color(sunBrightness, sunBrightness, 0)); // Bright yellow sun
	}
	
	// Draw sun rays occasionally
	if ((int)(sunPhase * 10) % 15 == 0) {
		for (int i = 0; i < 8; i++) {
			float rayAngle = i * (PI / 4.0) + sunPhase;
			float rayX = sunX + 2.5 * cos(rayAngle);
			float rayY = sunY + 2.5 * sin(rayAngle);
			
			int rayCol = (int)round(rayX);
			int rayRow = (int)round(rayY);
			
			if (rayCol >= 0 && rayCol < matrixCols && rayRow >= 0 && rayRow < matrixRows) {
				int rayPixelIdx = pixelIndex(rayCol, rayRow);
				myLedStrip.setPixelColor(rayPixelIdx, myLedStrip.Color(150, 150, 0)); // Dimmer sun rays
			}
		}
	}

	if (!windmill.active) {
		myLedStrip.show();
		return;
	}

	// Update windmill rotation
	windmill.currentRotation += windmill.rotationSpeed;
	if (windmill.currentRotation > 6.28) windmill.currentRotation -= 6.28; // 2*PI

	// Vary rotation speed slightly for dynamic effect
	if (random(100) < 2) {
		windmill.rotationSpeed = 0.08 + random(0, 80) / 1000.0; // 0.08 to 0.16
	}

	// Draw wider windmill blades
	for (int i = 0; i < 4; i++) {
		float bladeAngle = windmill.blades[i].angle + windmill.currentRotation;
		
		// Draw blade from center outward with increased width
		float bladeLength = windmill.blades[i].length;
		int steps = (int)(bladeLength * 1.5); // More steps for smoother lines
		
		for (int step = 2; step < steps; step++) { // Start from step 2 to leave center area
			float t = (float)step / steps;
			float distance = t * bladeLength;
			
			float bladeX = windmill.centerX + distance * cos(bladeAngle);
			float bladeY = windmill.centerY + distance * sin(bladeAngle);
			
			// Draw wider blades by drawing multiple pixels around each position
			for (int offsetX = -1; offsetX <= 1; offsetX++) {
				for (int offsetY = -1; offsetY <= 1; offsetY++) {
					int col = (int)round(bladeX) + offsetX;
					int row = (int)round(bladeY) + offsetY;
					
					if (col >= 0 && col < matrixCols && row >= 0 && row < matrixRows) {
						int pixelIdx = pixelIndex(col, row);
						
						// Blade gets brighter toward the tip for 3D effect
						float brightnessFactor = 0.6 + 0.4 * t;
						
						// Reduce brightness for edge pixels to create smooth width gradient
						float edgeFactor = 1.0;
						if (offsetX != 0 || offsetY != 0) {
							edgeFactor = 0.7; // Edge pixels dimmer for anti-aliasing effect
						}
						
						uint8_t bladeR = (uint8_t)(windmill.blades[i].r * brightnessFactor * edgeFactor);
						uint8_t bladeG = (uint8_t)(windmill.blades[i].g * brightnessFactor * edgeFactor);
						uint8_t bladeB = (uint8_t)(windmill.blades[i].b * brightnessFactor * edgeFactor);
						
						// Check if pixel already has content, if so blend
						uint32_t existingColor = myLedStrip.getPixelColor(pixelIdx);
						if (existingColor == 0) {
							myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(bladeR, bladeG, bladeB));
						} else {
							// Blend with existing color
							uint8_t existingR = (existingColor >> 16) & 0xFF;
							uint8_t existingG = (existingColor >> 8) & 0xFF;
							uint8_t existingB = existingColor & 0xFF;
							
							float alpha = 0.6; // Blade transparency
							bladeR = (uint8_t)(bladeR * alpha + existingR * (1.0 - alpha));
							bladeG = (uint8_t)(bladeG * alpha + existingG * (1.0 - alpha));
							bladeB = (uint8_t)(bladeB * alpha + existingB * (1.0 - alpha));
							
							myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(bladeR, bladeG, bladeB));
						}
					}
				}
			}
		}
	}

	// Draw windmill center hub
	int hubCol = (int)round(windmill.centerX);
	int hubRow = (int)round(windmill.centerY);
	
	if (hubCol >= 0 && hubCol < matrixCols && hubRow >= 0 && hubRow < matrixRows) {
		int hubPixelIdx = pixelIndex(hubCol, hubRow);
		myLedStrip.setPixelColor(hubPixelIdx, myLedStrip.Color(80, 60, 40)); // Dark brown hub
	}

	// Add some ground/grass at bottom
	if (matrixRows > 4) {
		for (int col = 0; col < matrixCols; col++) {
			int grassRow = matrixRows - 1;
			int pixelIdx = pixelIndex(col, grassRow);
			
			// Animated grass with slight color variation
			uint8_t grassVariation = (uint8_t)(20 * sin(col * 0.5 + sunPhase * 3));
			uint8_t grassG = 150 + grassVariation;
			uint8_t grassR = 20 + grassVariation / 4;
			
			myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(grassR, grassG, 30)); // Green grass
		}
	}

	myLedStrip.show();
}

// Pac-Man animation with dot eating
void updatePacmanAnimation() {
	if (millis() - lastPacmanUpdate < 120) return; // Update every 120ms for classic game speed
	lastPacmanUpdate = millis();

	myLedStrip.clear();

	// Toggle mouth animation
	if (millis() - pacman.lastMouthToggle > 200) {
		pacman.mouthOpen = !pacman.mouthOpen;
		pacman.lastMouthToggle = millis();
	}

	// Move Pac-Man
	if (millis() - pacman.lastMove > 300) { // Move every 300ms
		// Update position
		pacman.x += (pacman.direction == 0) ? 1 : (pacman.direction == 2) ? -1 : 0;
		pacman.y += (pacman.direction == 1) ? 1 : (pacman.direction == 3) ? -1 : 0;

		// Wrap around edges (tunnel effect)
		if (pacman.x < 0) pacman.x = matrixCols - 1;
		if (pacman.x >= matrixCols) pacman.x = 0;
		if (pacman.y < 0) pacman.y = matrixRows - 1;
		if (pacman.y >= matrixRows) pacman.y = 0;

		// Change direction occasionally for interesting movement
		if (random(100) < 15) { // 15% chance to change direction
			pacman.direction = random(0, 4);
		}

		pacman.lastMove = millis();
	}

	// Get Pac-Man position for collision detection and drawing
	int pacX = (int)round(pacman.x);
	int pacY = (int)round(pacman.y);

	// Check for dot collisions with bigger Pac-Man (3x3 area)
	for (int i = 0; i < MAX_DOTS; i++) {
		if (pacmanDots[i].active) {
			// Check if dot is within Pac-Man's 3x3 area
			bool eaten = false;
			for (int offsetX = -1; offsetX <= 1 && !eaten; offsetX++) {
				for (int offsetY = -1; offsetY <= 1 && !eaten; offsetY++) {
					int checkX = pacX + offsetX;
					int checkY = pacY + offsetY;
					
					if (pacmanDots[i].x == checkX && pacmanDots[i].y == checkY) {
						pacmanDots[i].active = false;
						pacman.dotsEaten++;
						eaten = true;
					}
				}
			}
		}
	}

	// Respawn dots if too many have been eaten
	int activeDots = 0;
	for (int i = 0; i < MAX_DOTS; i++) {
		if (pacmanDots[i].active) activeDots++;
	}
	
	if (activeDots < MAX_DOTS / 3) { // Respawn when less than 1/3 remain
		for (int i = 0; i < MAX_DOTS; i++) {
			if (!pacmanDots[i].active && random(100) < 30) { // 30% chance to respawn each dot
				pacmanDots[i].active = true;
				// Place new dot away from bigger Pac-Man (3x3 area)
				do {
					pacmanDots[i].x = random(0, matrixCols);
					pacmanDots[i].y = random(0, matrixRows);
				} while (abs(pacmanDots[i].x - pacX) <= 2 && abs(pacmanDots[i].y - pacY) <= 2);
			}
		}
	}

	// Draw dots with blinking effect
	for (int i = 0; i < MAX_DOTS; i++) {
		if (pacmanDots[i].active) {
			// Create blinking effect
			pacmanDots[i].brightness = (uint8_t)(128 + 127 * sin(millis() / 200.0 + i));
			
			int pixelIdx = pixelIndex(pacmanDots[i].x, pacmanDots[i].y);
			uint8_t brightness = pacmanDots[i].brightness;
			myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(
				brightness, brightness, brightness)); // White dots
		}
	}

	// Draw Pac-Man (bigger - 3x3 pixels)
	// Draw 3x3 Pac-Man body
	for (int offsetX = -1; offsetX <= 1; offsetX++) {
		for (int offsetY = -1; offsetY <= 1; offsetY++) {
			int drawX = pacX + offsetX;
			int drawY = pacY + offsetY;
			
			if (drawX >= 0 && drawX < matrixCols && drawY >= 0 && drawY < matrixRows) {
				int pixelIdx = pixelIndex(drawX, drawY);
				
				// Create mouth opening effect for bigger Pac-Man
				bool isMouth = false;
				if (pacman.mouthOpen) {
					// Create mouth opening based on direction
					switch (pacman.direction) {
						case 0: // Right
							isMouth = (offsetX == 1);
							break;
						case 1: // Down  
							isMouth = (offsetY == 1);
							break;
						case 2: // Left
							isMouth = (offsetX == -1);
							break;
						case 3: // Up
							isMouth = (offsetY == -1);
							break;
					}
				}
				
				if (!isMouth) {
					// Draw bright yellow Pac-Man body
					uint8_t brightness = 255;
					if (offsetX == 0 && offsetY == 0) {
						brightness = 255; // Center is brightest
					} else if ((abs(offsetX) + abs(offsetY)) == 1) {
						brightness = 230; // Adjacent pixels slightly dimmer
					} else {
						brightness = 200; // Corner pixels dimmer for round effect
					}
					
					myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(brightness, brightness, 0));
				} else {
					// Mouth area - draw darker yellow or blend with background
					uint32_t existingColor = myLedStrip.getPixelColor(pixelIdx);
					uint8_t existingR = (existingColor >> 16) & 0xFF;
					uint8_t existingG = (existingColor >> 8) & 0xFF;
					uint8_t existingB = existingColor & 0xFF;
					
					// Blend with existing color for mouth effect
					myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(
						constrain(80 + existingR, 0, 255),
						constrain(80 + existingG, 0, 255), 
						constrain(existingB, 0, 255))); // Dim yellow mouth
				}
			}
		}
	}

	// Add some trail effect behind Pac-Man (also bigger)
	int trailX = pacX - ((pacman.direction == 0) ? 2 : (pacman.direction == 2) ? -2 : 0);
	int trailY = pacY - ((pacman.direction == 1) ? 2 : (pacman.direction == 3) ? -2 : 0);
	
	// Handle wrap-around for trail
	if (trailX < 0) trailX = matrixCols + trailX;
	if (trailX >= matrixCols) trailX = trailX - matrixCols;
	if (trailY < 0) trailY = matrixRows + trailY;
	if (trailY >= matrixRows) trailY = trailY - matrixRows;
	
	// Draw 2x2 trail
	for (int offsetX = -1; offsetX <= 0; offsetX++) {
		for (int offsetY = -1; offsetY <= 0; offsetY++) {
			int drawX = trailX + offsetX;
			int drawY = trailY + offsetY;
			
			if (drawX >= 0 && drawX < matrixCols && drawY >= 0 && drawY < matrixRows) {
				int trailPixelIdx = pixelIndex(drawX, drawY);
				uint32_t existingColor = myLedStrip.getPixelColor(trailPixelIdx);
				uint8_t existingR = (existingColor >> 16) & 0xFF;
				uint8_t existingG = (existingColor >> 8) & 0xFF;
				uint8_t existingB = existingColor & 0xFF;
				
				// Add dim yellow trail
				myLedStrip.setPixelColor(trailPixelIdx, myLedStrip.Color(
					constrain(30 + existingR, 0, 255),
					constrain(30 + existingG, 0, 255), 
					constrain(existingB, 0, 255)));
			}
		}
	}

	myLedStrip.show();
}

// Snow falling animation
void updateSnowAnimation()
{
	if (millis() - lastSnowUpdate < 100)
		return; // Update every 100ms for smooth movement
	lastSnowUpdate = millis();

	myLedStrip.clear();

	// Update snowflake positions
	for (int i = 0; i < MAX_SNOWFLAKES; i++)
	{
		snowflakes[i].y += snowflakes[i].speed;

		// Reset snowflake if it falls off the bottom
		if (snowflakes[i].y >= matrixRows)
		{
			snowflakes[i].x = random(0, matrixCols * 100) / 100.0;
			snowflakes[i].y = random(-matrixRows, -1);
			snowflakes[i].speed = random(20, 60) / 100.0;
			snowflakes[i].brightness = random(100, 255);
		}

		// Draw snowflake if it's visible
		if (snowflakes[i].y >= 0 && snowflakes[i].y < matrixRows)
		{
			int col = (int)snowflakes[i].x;
			int row = (int)snowflakes[i].y;
			if (col >= 0 && col < matrixCols)
			{
				int pixelIdx = pixelIndex(col, row);
				uint32_t snowColor = myLedStrip.Color(
					snowflakes[i].brightness,
					snowflakes[i].brightness,
					snowflakes[i].brightness);
				myLedStrip.setPixelColor(pixelIdx, snowColor);
			}
		}
	}

	myLedStrip.show();
}

// Ocean wave animation
void updateOceanAnimation()
{
	if (millis() - lastOceanUpdate < 150)
		return; // Update every 150ms
	lastOceanUpdate = millis();

	myLedStrip.clear();

	// Update wave phases
	wavePhase += 0.3;
	foamOffset += 0.2;
	if (wavePhase > 6.28)
		wavePhase -= 6.28; // 2*PI
	if (foamOffset > 6.28)
		foamOffset -= 6.28;

	// Draw ocean waves
	for (int col = 0; col < matrixCols; col++)
	{
		// Create wave pattern with multiple sine waves for complexity
		float wave1 = sin(wavePhase + col * 0.3) * 1.5;
		float wave2 = sin(wavePhase * 1.5 + col * 0.2) * 0.8;
		float waveHeight = wave1 + wave2;

		// Map wave height to row position (bottom half of matrix)
		int baseWaterLevel = matrixRows * 0.7; // Water starts at 70% down
		int waveRow = baseWaterLevel + (int)waveHeight;
		waveRow = constrain(waveRow, 0, matrixRows - 1);

		// Draw water (blue gradient from deep to shallow)
		for (int row = waveRow; row < matrixRows; row++)
		{
			int depth = matrixRows - row;
			uint8_t blue = map(depth, 1, matrixRows - waveRow + 1, 100, 255);
			uint8_t green = map(depth, 1, matrixRows - waveRow + 1, 20, 100);
			int pixelIdx = pixelIndex(col, row);
			myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(0, green, blue));
		}

		// Add foam on wave crests
		float foamWave = sin(foamOffset + col * 0.4);
		if (foamWave > 0.3 && waveRow > 0)
		{
			uint8_t foamBrightness = (uint8_t)map(foamWave * 100, 30, 100, 100, 255);
			int foamRow = waveRow - 1;
			if (foamRow >= 0)
			{
				int pixelIdx = pixelIndex(col, foamRow);
				myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(
													   foamBrightness, foamBrightness, foamBrightness));
			}
		}
	}

	myLedStrip.show();
}

// Initialize fireworks animation
void initFireworksAnimation() {
	// Clear all particles
	for (int i = 0; i < MAX_PARTICLES; i++) {
		particles[i].life = 0;
	}
	lastFireworkLaunch = millis();
}

// Initialize rotating hexagons animation
void initRotatingHexagonsAnimation() {
	// Create hexagons at different positions and sizes
	for (int i = 0; i < MAX_HEXAGONS; i++) {
		hexagons[i].active = true;
		hexagons[i].centerX = (matrixCols * (i + 1)) / (float)(MAX_HEXAGONS + 1);
		hexagons[i].centerY = matrixRows * 0.5;
		hexagons[i].rotation = (i * PI) / 3.0; // Start at different angles
		hexagons[i].rotationSpeed = 0.05 + (i * 0.02); // Different rotation speeds
		hexagons[i].size = 2.0 + (i % 3) * 0.5; // Varying sizes
		
		// Initialize with rainbow colors
		float hue = (i * 60.0); // 60 degrees apart in hue
		float c = 1.0; // Full saturation
		float x = c * (1.0 - abs(fmod(hue / 60.0, 2) - 1.0));
		
		float r, g, b;
		if (hue >= 0 && hue < 60) {
			r = c; g = x; b = 0;
		} else if (hue >= 60 && hue < 120) {
			r = x; g = c; b = 0;
		} else if (hue >= 120 && hue < 180) {
			r = 0; g = c; b = x;
		} else if (hue >= 180 && hue < 240) {
			r = 0; g = x; b = c;
		} else if (hue >= 240 && hue < 300) {
			r = x; g = 0; b = c;
		} else {
			r = c; g = 0; b = x;
		}
		
		hexagons[i].r = (uint8_t)(r * 255);
		hexagons[i].g = (uint8_t)(g * 255);
		hexagons[i].b = (uint8_t)(b * 255);
	}
	globalRotation = 0.0;
	colorPhase = 0.0;
}

// Initialize Qix animation
void initQixAnimation() {
	qixObject.active = true;
	qixObject.segmentCount = 12;
	qixObject.segmentSpacing = 0.8;
	qixObject.lastColorChange = millis();
	
	// Random starting position for head (avoid exact edges)
	qixObject.headX = random(20, (matrixCols - 2) * 10) / 10.0;
	qixObject.headY = random(20, (matrixRows - 2) * 10) / 10.0;
	
	// Random initial velocity
	qixObject.vx = random(-50, 50) / 100.0;
	qixObject.vy = random(-50, 50) / 100.0;
	
	// Ensure minimum speed
	if (abs(qixObject.vx) < 0.2) qixObject.vx = (qixObject.vx < 0) ? -0.2 : 0.2;
	if (abs(qixObject.vy) < 0.2) qixObject.vy = (qixObject.vy < 0) ? -0.2 : 0.2;
	
	// Initialize all segments starting at head position
	for (int i = 0; i < qixObject.segmentCount; i++) {
		qixObject.segments[i].x = qixObject.headX;
		qixObject.segments[i].y = qixObject.headY;
		
		// Create rainbow color pattern along segments
		float hueStep = 360.0 / qixObject.segmentCount;
		float hue = i * hueStep;
		
		// Convert HSV to RGB for rainbow effect
		float c = 1.0; // Full saturation
		float x = c * (1.0 - abs(fmod(hue / 60.0, 2) - 1.0));
		float m = 0.0; // Full value/brightness
		
		float r, g, b;
		if (hue >= 0 && hue < 60) {
			r = c; g = x; b = 0;
		} else if (hue >= 60 && hue < 120) {
			r = x; g = c; b = 0;
		} else if (hue >= 120 && hue < 180) {
			r = 0; g = c; b = x;
		} else if (hue >= 180 && hue < 240) {
			r = 0; g = x; b = c;
		} else if (hue >= 240 && hue < 300) {
			r = x; g = 0; b = c;
		} else {
			r = c; g = 0; b = x;
		}
		
		qixObject.segments[i].r = (uint8_t)(r * 255);
		qixObject.segments[i].g = (uint8_t)(g * 255);
		qixObject.segments[i].b = (uint8_t)(b * 255);
	}
}

// Launch a new firework (either Chrysanthemum or Spider type)
void launchFirework() {
	float centerX = random(matrixCols * 20, matrixCols * 80) / 100.0; // Random X position (avoid edges)
	float centerY = random(10, matrixRows * 60) / 100.0; // Random Y position (upper portion)
	
	// Randomly choose firework type (50% chance each)
	bool isSpiderType = random(0, 2) == 1;
	
	// Choose colors based on type
	uint8_t baseR, baseG, baseB;
	if (isSpiderType) {
		// Spider: bright white/silver with blue tint
		baseR = 200; baseG = 220; baseB = 255;
	} else {
		// Chrysanthemum: warm colors (red, orange, yellow, gold)
		uint8_t colorType = random(0, 4);
		switch(colorType) {
			case 0: // Red
				baseR = 255; baseG = 80; baseB = 80;
				break;
			case 1: // Orange
				baseR = 255; baseG = 165; baseB = 0;
				break;
			case 2: // Yellow
				baseR = 255; baseG = 255; baseB = 100;
				break;
			default: // Gold
				baseR = 255; baseG = 215; baseB = 0;
				break;
		}
	}
	
	// Create explosion particles
	int particlesCreated = 0;
	int particleCount = isSpiderType ? 8 : 16; // Spider has fewer but more linear particles
	
	for (int i = 0; i < MAX_PARTICLES && particlesCreated < particleCount; i++) {
		if (particles[i].life == 0) { // Find empty slot
			float angle, speed;
			
			if (isSpiderType) {
				// Spider: straight radial lines, evenly spaced
				angle = (particlesCreated * 2.0 * PI) / particleCount;
				speed = random(150, 200) / 100.0; // Fast, consistent speed
			} else {
				// Chrysanthemum: random spherical explosion
				angle = random(0, 628) / 100.0; // Random angle in radians * 100
				speed = random(80, 140) / 100.0; // Variable explosion speed
			}
			
			particles[i].x = centerX;
			particles[i].y = centerY;
			particles[i].vx = cos(angle) * speed;
			particles[i].vy = sin(angle) * speed;
			particles[i].life = isSpiderType ? random(120, 180) : random(200, 255);
			particles[i].isSpider = isSpiderType;
			particles[i].trailLength = isSpiderType ? 1 : random(3, 7); // Spider no trail, Chrysanthemum has trails
			particles[i].gravity = isSpiderType ? 0.05 : 0.12; // Spider less gravity, falls slower
			
			// Add some color variation
			particles[i].r = constrain(baseR + random(-20, 20), 0, 255);
			particles[i].g = constrain(baseG + random(-20, 20), 0, 255);
			particles[i].b = constrain(baseB + random(-20, 20), 0, 255);
			
			particlesCreated++;
		}
	}
}

// Fireworks animation with Chrysanthemum and Spider types
void updateFireworksAnimation() {
	if (millis() - lastFireworkUpdate < 60) return; // Update every 60ms for smooth animation
	lastFireworkUpdate = millis();

	myLedStrip.clear();

	// Launch new firework if it's time
	if (millis() - lastFireworkLaunch > FIREWORK_INTERVAL) {
		launchFirework();
		lastFireworkLaunch = millis();
	}

	// Update and draw particles
	for (int i = 0; i < MAX_PARTICLES; i++) {
		if (particles[i].life > 0) {
			// Update particle position
			particles[i].x += particles[i].vx;
			particles[i].y += particles[i].vy;
			
			// Apply gravity (different for each type)
			particles[i].vy += particles[i].gravity;
			
			// Reduce particle life (fade out)
			if (particles[i].isSpider) {
				particles[i].life -= 4; // Spider fades slower
			} else {
				particles[i].life -= 6; // Chrysanthemum fades faster
			}
			
			// Draw particle and trail if it's within bounds
			int col = (int)particles[i].x;
			int row = (int)particles[i].y;
			
			if (col >= 0 && col < matrixCols && row >= 0 && row < matrixRows) {
				// Calculate brightness based on remaining life
				float lifeFactor = particles[i].life / 255.0;
				uint8_t brightness = (uint8_t)(lifeFactor * 255);
				
				uint8_t r = (particles[i].r * brightness) / 255;
				uint8_t g = (particles[i].g * brightness) / 255;
				uint8_t b = (particles[i].b * brightness) / 255;
				
				int pixelIdx = pixelIndex(col, row);
				
				// Draw main particle
				uint32_t existingColor = myLedStrip.getPixelColor(pixelIdx);
				uint8_t existingR = (existingColor >> 16) & 0xFF;
				uint8_t existingG = (existingColor >> 8) & 0xFF;
				uint8_t existingB = existingColor & 0xFF;
				
				// Additive blending for sparkle effect
				r = constrain(r + existingR, 0, 255);
				g = constrain(g + existingG, 0, 255);
				b = constrain(b + existingB, 0, 255);
				
				myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(r, g, b));
				
				// Draw trail for Chrysanthemum (visible sparks)
				if (!particles[i].isSpider && particles[i].trailLength > 1) {
					for (int t = 1; t < particles[i].trailLength; t++) {
						float trailX = particles[i].x - (particles[i].vx * t * 0.3);
						float trailY = particles[i].y - (particles[i].vy * t * 0.3);
						
						int trailCol = (int)trailX;
						int trailRow = (int)trailY;
						
						if (trailCol >= 0 && trailCol < matrixCols && trailRow >= 0 && trailRow < matrixRows) {
							float trailBrightness = brightness * (1.0 - (float)t / particles[i].trailLength) * 0.6;
							uint8_t trailR = (particles[i].r * (uint8_t)trailBrightness) / 255;
							uint8_t trailG = (particles[i].g * (uint8_t)trailBrightness) / 255;
							uint8_t trailB = (particles[i].b * (uint8_t)trailBrightness) / 255;
							
							int trailIdx = pixelIndex(trailCol, trailRow);
							uint32_t trailExisting = myLedStrip.getPixelColor(trailIdx);
							uint8_t trailExistingR = (trailExisting >> 16) & 0xFF;
							uint8_t trailExistingG = (trailExisting >> 8) & 0xFF;
							uint8_t trailExistingB = trailExisting & 0xFF;
							
							trailR = constrain(trailR + trailExistingR, 0, 255);
							trailG = constrain(trailG + trailExistingG, 0, 255);
							trailB = constrain(trailB + trailExistingB, 0, 255);
							
							myLedStrip.setPixelColor(trailIdx, myLedStrip.Color(trailR, trailG, trailB));
						}
					}
				}
			}
		}
	}

	myLedStrip.show();
}

// Draw a hexagon at given center with rotation
void drawHexagon(float centerX, float centerY, float size, float rotation, uint8_t r, uint8_t g, uint8_t b) {
	// Hexagon vertices (6 points)
	const int numVertices = 6;
	float vertices[numVertices][2];
	
	// Calculate hexagon vertices
	for (int i = 0; i < numVertices; i++) {
		float angle = rotation + (i * PI / 3.0); // 60 degrees between vertices
		vertices[i][0] = centerX + cos(angle) * size;
		vertices[i][1] = centerY + sin(angle) * size;
	}
	
	// Draw hexagon edges
	for (int i = 0; i < numVertices; i++) {
		int nextVertex = (i + 1) % numVertices;
		
		// Draw line between current vertex and next vertex
		float x1 = vertices[i][0];
		float y1 = vertices[i][1];
		float x2 = vertices[nextVertex][0];
		float y2 = vertices[nextVertex][1];
		
		// Simple line drawing using Bresenham-like approach
		float dx = x2 - x1;
		float dy = y2 - y1;
		float steps = max(abs(dx), abs(dy));
		
		if (steps > 0) {
			float xIncrement = dx / steps;
			float yIncrement = dy / steps;
			
			float x = x1;
			float y = y1;
			
			for (int step = 0; step <= (int)steps; step++) {
				int col = (int)round(x);
				int row = (int)round(y);
				
				if (col >= 0 && col < matrixCols && row >= 0 && row < matrixRows) {
					int pixelIdx = pixelIndex(col, row);
					
					// Additive blending for overlapping hexagons
					uint32_t existingColor = myLedStrip.getPixelColor(pixelIdx);
					uint8_t existingR = (existingColor >> 16) & 0xFF;
					uint8_t existingG = (existingColor >> 8) & 0xFF;
					uint8_t existingB = existingColor & 0xFF;
					
					uint8_t newR = constrain(r + existingR, 0, 255);
					uint8_t newG = constrain(g + existingG, 0, 255);
					uint8_t newB = constrain(b + existingB, 0, 255);
					
					myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(newR, newG, newB));
				}
				
				x += xIncrement;
				y += yIncrement;
			}
		}
	}
}

// Rotating hexagons animation
void updateRotatingHexagonsAnimation() {
	if (millis() - lastHexagonUpdate < 100) return; // Update every 100ms
	lastHexagonUpdate = millis();

	myLedStrip.clear();

	// Update global rotation and color phase
	globalRotation += 0.02;
	colorPhase += 0.05;
	if (globalRotation > 2 * PI) globalRotation -= 2 * PI;
	if (colorPhase > 2 * PI) colorPhase -= 2 * PI;

	// Update and draw each hexagon
	for (int i = 0; i < MAX_HEXAGONS; i++) {
		if (hexagons[i].active) {
			// Update individual rotation
			hexagons[i].rotation += hexagons[i].rotationSpeed;
			if (hexagons[i].rotation > 2 * PI) hexagons[i].rotation -= 2 * PI;
			
			// Add global rotation for mutual rotation effect
			float totalRotation = hexagons[i].rotation + globalRotation;
			
			// Calculate orbital position (hexagons rotate around each other)
			float orbitRadius = 1.0;
			float orbitAngle = globalRotation * 0.5 + (i * 2 * PI / MAX_HEXAGONS);
			float newCenterX = hexagons[i].centerX + cos(orbitAngle) * orbitRadius;
			float newCenterY = hexagons[i].centerY + sin(orbitAngle) * orbitRadius;
			
			// Update colors with shifting rainbow effect
			float hue = fmod((i * 60.0) + (colorPhase * 180.0 / PI), 360.0);
			float c = 1.0;
			float x = c * (1.0 - abs(fmod(hue / 60.0, 2) - 1.0));
			
			float r, g, b;
			if (hue >= 0 && hue < 60) {
				r = c; g = x; b = 0;
			} else if (hue >= 60 && hue < 120) {
				r = x; g = c; b = 0;
			} else if (hue >= 120 && hue < 180) {
				r = 0; g = c; b = x;
			} else if (hue >= 180 && hue < 240) {
				r = 0; g = x; b = c;
			} else if (hue >= 240 && hue < 300) {
				r = x; g = 0; b = c;
			} else {
				r = c; g = 0; b = x;
			}
			
			uint8_t red = (uint8_t)(r * 200); // Slightly dimmer for better visibility
			uint8_t green = (uint8_t)(g * 200);
			uint8_t blue = (uint8_t)(b * 200);
			
			// Draw the hexagon
			drawHexagon(newCenterX, newCenterY, hexagons[i].size, totalRotation, red, green, blue);
		}
	}

	myLedStrip.show();
}

// Qix animation with bouncing colorful multi-segment object
void updateQixAnimation() {
	if (millis() - lastQixUpdate < 80) return; // Update every 80ms for smooth movement
	lastQixUpdate = millis();

	myLedStrip.clear();

	if (!qixObject.active) return;

	// Store previous head position
	float prevHeadX = qixObject.headX;
	float prevHeadY = qixObject.headY;

	// Update head position
	qixObject.headX += qixObject.vx * qixSpeedMultiplier;
	qixObject.headY += qixObject.vy * qixSpeedMultiplier;

	// Bounce off edges with slight randomness
	bool bounced = false;
	if (qixObject.headX <= 0 || qixObject.headX >= matrixCols - 1) {
		qixObject.vx = -qixObject.vx;
		qixObject.vx += random(-15, 15) / 100.0; // Add randomness
		qixObject.headX = constrain(qixObject.headX, 0, matrixCols - 1);
		bounced = true;
	}
	
	if (qixObject.headY <= 0 || qixObject.headY >= matrixRows - 1) {
		qixObject.vy = -qixObject.vy;
		qixObject.vy += random(-15, 15) / 100.0; // Add randomness
		qixObject.headY = constrain(qixObject.headY, 0, matrixRows - 1);
		bounced = true;
	}

	// Ensure speed doesn't get too low or too high
	qixObject.vx = constrain(qixObject.vx, -1.2, 1.2);
	qixObject.vy = constrain(qixObject.vy, -1.2, 1.2);
	
	if (abs(qixObject.vx) < 0.15) qixObject.vx = (qixObject.vx < 0) ? -0.15 : 0.15;
	if (abs(qixObject.vy) < 0.15) qixObject.vy = (qixObject.vy < 0) ? -0.15 : 0.15;

	// Update segment positions - each segment follows the one in front
	// Move segments from tail to head to avoid overwriting positions
	for (int i = qixObject.segmentCount - 1; i >= 0; i--) {
		if (i == 0) {
			// First segment follows the head
			float dx = qixObject.headX - qixObject.segments[i].x;
			float dy = qixObject.headY - qixObject.segments[i].y;
			float distance = sqrt(dx * dx + dy * dy);
			
			if (distance > qixObject.segmentSpacing) {
				float ratio = qixObject.segmentSpacing / distance;
				qixObject.segments[i].x = qixObject.headX - dx * ratio;
				qixObject.segments[i].y = qixObject.headY - dy * ratio;
			}
		} else {
			// Other segments follow the segment in front
			float dx = qixObject.segments[i-1].x - qixObject.segments[i].x;
			float dy = qixObject.segments[i-1].y - qixObject.segments[i].y;
			float distance = sqrt(dx * dx + dy * dy);
			
			if (distance > qixObject.segmentSpacing) {
				float ratio = qixObject.segmentSpacing / distance;
				qixObject.segments[i].x = qixObject.segments[i-1].x - dx * ratio;
				qixObject.segments[i].y = qixObject.segments[i-1].y - dy * ratio;
			}
		}
	}

	// Shift colors along segments periodically for dynamic effect
	if (millis() - qixObject.lastColorChange > 200) {
		qixObject.lastColorChange = millis();
		
		// Store the last segment's color
		uint8_t lastR = qixObject.segments[qixObject.segmentCount - 1].r;
		uint8_t lastG = qixObject.segments[qixObject.segmentCount - 1].g;
		uint8_t lastB = qixObject.segments[qixObject.segmentCount - 1].b;
		
		// Shift colors from tail to head
		for (int i = qixObject.segmentCount - 1; i > 0; i--) {
			qixObject.segments[i].r = qixObject.segments[i - 1].r;
			qixObject.segments[i].g = qixObject.segments[i - 1].g;
			qixObject.segments[i].b = qixObject.segments[i - 1].b;
		}
		
		// Put the last color at the front
		qixObject.segments[0].r = lastR;
		qixObject.segments[0].g = lastG;
		qixObject.segments[0].b = lastB;
	}

	// Draw connecting lines between segments
	for (int i = 0; i < qixObject.segmentCount; i++) {
		// Draw the segment point
		int col = (int)round(qixObject.segments[i].x);
		int row = (int)round(qixObject.segments[i].y);
		
		if (col >= 0 && col < matrixCols && row >= 0 && row < matrixRows) {
			int pixelIdx = pixelIndex(col, row);
			myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(
				qixObject.segments[i].r,
				qixObject.segments[i].g,
				qixObject.segments[i].b
			));
		}
		
		// Draw line to next segment (simple line drawing)
		if (i < qixObject.segmentCount - 1) {
			float x1 = qixObject.segments[i].x;
			float y1 = qixObject.segments[i].y;
			float x2 = qixObject.segments[i + 1].x;
			float y2 = qixObject.segments[i + 1].y;
			
			// Interpolate between points
			float dx = x2 - x1;
			float dy = y2 - y1;
			float distance = sqrt(dx * dx + dy * dy);
			
			if (distance > 0) {
				int steps = (int)(distance * 2); // More steps for smoother lines
				for (int j = 0; j <= steps; j++) {
					float t = (float)j / steps;
					float x = x1 + dx * t;
					float y = y1 + dy * t;
					
					int col = (int)round(x);
					int row = (int)round(y);
					
					if (col >= 0 && col < matrixCols && row >= 0 && row < matrixRows) {
						int pixelIdx = pixelIndex(col, row);
						
						// Blend colors along the line
						uint8_t r = (uint8_t)(qixObject.segments[i].r * (1 - t) + qixObject.segments[i + 1].r * t);
						uint8_t g = (uint8_t)(qixObject.segments[i].g * (1 - t) + qixObject.segments[i + 1].g * t);
						uint8_t b = (uint8_t)(qixObject.segments[i].b * (1 - t) + qixObject.segments[i + 1].b * t);
						
						// Blend with existing color for overlapping lines
						uint32_t existingColor = myLedStrip.getPixelColor(pixelIdx);
						uint8_t existingR = (existingColor >> 16) & 0xFF;
						uint8_t existingG = (existingColor >> 8) & 0xFF;
						uint8_t existingB = existingColor & 0xFF;
						
						r = constrain(r + existingR / 2, 0, 255);
						g = constrain(g + existingG / 2, 0, 255);
						b = constrain(b + existingB / 2, 0, 255);
						
						myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(r, g, b));
					}
				}
			}
		}
	}

	// Draw the head with extra brightness
	int headCol = (int)round(qixObject.headX);
	int headRow = (int)round(qixObject.headY);
	
	if (headCol >= 0 && headCol < matrixCols && headRow >= 0 && headRow < matrixRows) {
		int pixelIdx = pixelIndex(headCol, headRow);
		// Head gets bright white color
		myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(255, 255, 255));
	}

	// Occasionally vary the speed for dynamic effect
	if (random(1000) < 3) {
		qixSpeedMultiplier = random(60, 120) / 100.0;
	}

	myLedStrip.show();
}

// Helper function to create horizontally flipped version of pixelIndex
int pixelIndexFlipped(int col, int row)
{
	// bounds safety
	if (col < 0)
		col = 0;
	if (col >= matrixCols)
		col = matrixCols - 1;
	if (row < 0)
		row = 0;
	if (row >= matrixRows)
		row = matrixRows - 1;

	// Flip horizontally by inverting the column
	int flippedCol = matrixCols - 1 - col;
	
	int base = row * matrixCols;
	if ((row & 1) == 0)
	{
		// even rows go left->right
		return base + flippedCol;
	}
	else
	{
		// odd rows go right->left (serpentine)
		return base + (matrixCols - 1 - flippedCol);
	}
}

// Temperature display animation
void updateTemperatureAnimation() {
	if (millis() - lastTempUpdate < 500) return; // Update every 500ms
	lastTempUpdate = millis();

	myLedStrip.clear();

	// Determine if we should flip horizontally (middle 6 seconds of 30-second cycle)
	unsigned long animationElapsed = millis() - lastAnimationChange;
	unsigned long flipStartTime = 12000; // Start flip at 12 seconds
	unsigned long flipEndTime = 18000;   // End flip at 18 seconds (6-second duration)
	bool shouldFlip = (animationElapsed >= flipStartTime && animationElapsed < flipEndTime);

	// Only update display if we have valid temperature data
	if (currentTemperature > -999.0) {
		displayedTemperature = currentTemperature;
	}

	// Format temperature string (e.g., "23'C")
	String tempStr;
	if (displayedTemperature > -999.0) {
		tempStr = String(displayedTemperature, 0) + "'C";
	} else {
		tempStr = "--'C";
	}

	// Calculate starting position to center the text
	int textWidth = tempStr.length() * 4 + (tempStr.length() - 1); // 4 cols per char + 1 spacing
	int startCol = (matrixCols - textWidth) / 2;
	if (startCol < 0) startCol = 0;

	// Draw each character
	int currentCol = startCol;
	for (int i = 0; i < tempStr.length(); i++) {
		char c = tempStr.charAt(i);
		int patternIndex = -1;

		// Map character to pattern index
		if (c >= '0' && c <= '9') {
			patternIndex = c - '0'; // 0-9
		} else if (c == '.') {
			patternIndex = 10; // decimal point
		} else if (c == '\'') {
			patternIndex = 11; // degree symbol
		} else if (c == 'C') {
			patternIndex = 12; // C
		} else if (c == '-') {
			// Draw dash manually
			for (int col = 0; col < 3 && (currentCol + col) < matrixCols; col++) {
				int pixelIdx = shouldFlip ? pixelIndexFlipped(currentCol + col, 3) : pixelIndex(currentCol + col, 3);
				myLedStrip.setPixelColor(pixelIdx, myLedStrip.Color(255, 100, 0)); // Orange
			}
			currentCol += 4; // Move to next character position
			continue;
		}

		// Draw the pattern if we found a valid one
		if (patternIndex >= 0) {
			for (int row = 0; row < 7; row++) {
				for (int col = 0; col < 4 && (currentCol + col) < matrixCols; col++) {
					if (digitPatterns[patternIndex][row][col]) {
						int pixelIdx = shouldFlip ? pixelIndexFlipped(currentCol + col, row) : pixelIndex(currentCol + col, row);
						// Color coding: normal temp (20-25°C) = green, cold = blue, hot = red
						uint32_t color;
						if (displayedTemperature < 0.0) {
							color = myLedStrip.Color(0, 100, 255); // Blue for cold
						} else if (displayedTemperature > 20.0) {
							color = myLedStrip.Color(255, 50, 0); // Red for hot
						} else {
							color = myLedStrip.Color(0, 255, 50); // Green for comfortable
						}
						myLedStrip.setPixelColor(pixelIdx, color);
					}
				}
			}
		}

		currentCol += 4; // Move to next character position (4 cols + 1 spacing, but we'll add spacing below)
		if (i < tempStr.length() - 1) currentCol += 1; // Add spacing between characters
	}

	myLedStrip.show();
}

// Function to control the builtin RGB LED
void setBuiltinRGB(uint8_t red, uint8_t green, uint8_t blue)
{
	neopixelWrite(RGB_BUILTIN, red, green, blue);
}

// WiFi and Web Server Functions
void loadWiFiCredentials()
{
	preferences.begin("wifi", false);
	savedSSID = preferences.getString("ssid", "");
	savedPassword = preferences.getString("password", "");
	preferences.end();

	Serial.print("Loaded WiFi credentials - SSID: ");
	Serial.println(savedSSID);
}

void saveWiFiCredentials(String ssid, String password)
{
	preferences.begin("wifi", false);
	preferences.putString("ssid", ssid);
	preferences.putString("password", password);
	preferences.end();

	savedSSID = ssid;
	savedPassword = password;
	Serial.println("WiFi credentials saved");
}

void clearWiFiCredentials()
{
	preferences.begin("wifi", false);
	preferences.clear();
	preferences.end();

	savedSSID = "";
	savedPassword = "";
	Serial.println("WiFi credentials cleared");
}

void loadOTAPassword()
{
	preferences.begin("ota", false);
	savedOTAPassword = preferences.getString("password", DEFAULT_OTA_PASSWORD);
	preferences.end();

	Serial.println("OTA password loaded");
}

void saveOTAPassword(String password)
{
	preferences.begin("ota", false);
	preferences.putString("password", password);
	preferences.end();

	savedOTAPassword = password;
	Serial.println("OTA password saved");
}

void resetOTAPassword()
{
	preferences.begin("ota", false);
	preferences.clear();
	preferences.end();

	savedOTAPassword = DEFAULT_OTA_PASSWORD;
	Serial.println("OTA password reset to default");
}

// NeoPixel type configuration functions
void loadNeoPixelType()
{
	preferences.begin("neopixel", false);
	int savedType = preferences.getInt("type", NEOPIXEL_GRB);
	preferences.end();

	// Validate the loaded type
	if (savedType >= NEOPIXEL_GRB && savedType <= NEOPIXEL_WRGB) {
		currentNeoPixelType = (NeoPixelType)savedType;
	} else {
		currentNeoPixelType = NEOPIXEL_GRB; // Default fallback
	}

	// Update the current pixel configuration
	switch(currentNeoPixelType) {
		case NEOPIXEL_GRB:
			currentPixelConfig = NEO_GRB + NEO_KHZ800;
			Serial.println("Loaded NeoPixel type: NEO_GRB + NEO_KHZ800");
			break;
		case NEOPIXEL_BGR:
			currentPixelConfig = NEO_BGR + NEO_KHZ800;
			Serial.println("Loaded NeoPixel type: NEO_BGR + NEO_KHZ800");
			break;
		case NEOPIXEL_RGB:
			currentPixelConfig = NEO_RGB + NEO_KHZ800;
			Serial.println("Loaded NeoPixel type: NEO_RGB + NEO_KHZ800");
			break;
		case NEOPIXEL_WRGB:
			currentPixelConfig = NEO_WRGB + NEO_KHZ800;
			Serial.println("Loaded NeoPixel type: NEO_WRGB + NEO_KHZ800");
			break;
	}
}

void saveNeoPixelType(NeoPixelType type)
{
	preferences.begin("neopixel", false);
	preferences.putInt("type", (int)type);
	preferences.end();

	currentNeoPixelType = type;
	
	// Update the current pixel configuration
	switch(currentNeoPixelType) {
		case NEOPIXEL_GRB:
			currentPixelConfig = NEO_GRB + NEO_KHZ800;
			Serial.println("Saved NeoPixel type: NEO_GRB + NEO_KHZ800");
			break;
		case NEOPIXEL_BGR:
			currentPixelConfig = NEO_BGR + NEO_KHZ800;
			Serial.println("Saved NeoPixel type: NEO_BGR + NEO_KHZ800");
			break;
		case NEOPIXEL_RGB:
			currentPixelConfig = NEO_RGB + NEO_KHZ800;
			Serial.println("Saved NeoPixel type: NEO_RGB + NEO_KHZ800");
			break;
		case NEOPIXEL_WRGB:
			currentPixelConfig = NEO_WRGB + NEO_KHZ800;
			Serial.println("Saved NeoPixel type: NEO_WRGB + NEO_KHZ800");
			break;
	}
}

String getNeoPixelTypeName(NeoPixelType type)
{
	switch(type) {
		case NEOPIXEL_GRB:
			return "NEO_GRB + NEO_KHZ800";
		case NEOPIXEL_BGR:
			return "NEO_BGR + NEO_KHZ800";
		case NEOPIXEL_RGB:
			return "NEO_RGB + NEO_KHZ800";
		case NEOPIXEL_WRGB:
			return "NEO_WRGB + NEO_KHZ800";
		default:
			return "NEO_GRB + NEO_KHZ800";
	}
}

bool connectToWiFi()
{
	if (savedSSID.length() == 0)
	{
		Serial.println("No saved WiFi credentials");
		return false;
	}

	Serial.print("Connecting to WiFi: ");
	Serial.println(savedSSID);

	WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

	int attempts = 0;
	while (WiFi.status() != WL_CONNECTED && attempts < 20)
	{
		delay(500);
		Serial.print(".");
		attempts++;
	}

	if (WiFi.status() == WL_CONNECTED)
	{
		Serial.println();
		Serial.print("WiFi connected! IP address: ");
		Serial.println(WiFi.localIP());
		return true;
	}
	else
	{
		Serial.println();
		Serial.println("Failed to connect to WiFi");
		return false;
	}
}

void startAccessPoint()
{
	Serial.println("Starting Access Point for WiFi configuration...");

	WiFi.mode(WIFI_AP);
	WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
	WiFi.softAP(AP_SSID, AP_PASSWORD);

	// Start DNS server for captive portal
	dnsServer.start(53, "*", AP_IP);

	Serial.print("Access Point started. Connect to: ");
	Serial.println(AP_SSID);
	Serial.print("Password: ");
	Serial.println(AP_PASSWORD);
	Serial.print("Configuration page: http://");
	Serial.println(WiFi.softAPIP());

	wifiConfigMode = true;
}

void handleRoot()
{
	String html = "<!DOCTYPE html><html><head>";
	html += "<meta charset='UTF-8'>";
	html += "<title>S3Serpentine WiFi Setup</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
	html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:linear-gradient(135deg, #4facfe 0%, #00f2fe 25%, #03dac6 50%, #018786 75%, #004d40 100%);min-height:100vh;background-attachment:fixed}";
	html += ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
	html += "input[type=text],input[type=password]{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:5px}";
	html += "input[type=submit]{background:#4CAF50;color:white;padding:12px 20px;border:none;border-radius:5px;cursor:pointer;width:100%}";
	html += "input[type=submit]:hover{background:#45a049}";
	html += ".radio-group{margin:15px 0}.radio-option{margin:8px 0;padding:10px;border:2px solid #ddd;border-radius:8px;background:#f9f9f9;cursor:pointer;transition:all 0.3s}";
	html += ".radio-option:hover{border-color:#4CAF50;background:#f0f8f0}.radio-option.selected{border-color:#4CAF50;background:#e8f5e8}";
	html += ".radio-option input[type=radio]{margin-right:10px}label{cursor:pointer;display:block}</style></head><body>";
	html += "<div class='container'><h2>S3Serpentine WiFi Configuration</h2>";

	if (wifiConfigMode)
	{
		html += "<form action='/save' method='POST'>";
		html += "<label for='ssid'>WiFi Network Name (SSID):</label>";
		html += "<input type='text' id='ssid' name='ssid' required>";
		html += "<label for='password'>WiFi Password:</label>";
		html += "<input type='text' id='password' name='password'>";
		html += "<input type='submit' value='Save and Connect'>";
		html += "</form>";
		html += "<p><small>After saving, the device will restart and connect to your WiFi network.</small></p>";
	}
	else
	{
		html += "<h3>LED Animation Control</h3>";
		String currentAnimName;
		switch(currentAnimation) {
			case ANIMATION_ROTATING_HEXAGONS: currentAnimName = "Rotating Hexagons"; break;
			case ANIMATION_MATRIX_RAIN: currentAnimName = "Matrix Rain"; break;
			case ANIMATION_POLISH_FLAG: currentAnimName = "Polish Flag"; break;
			case ANIMATION_UKRAINIAN_FLAG: currentAnimName = "Ukrainian Flag"; break;
			case ANIMATION_WINDMILL: currentAnimName = "Windmill"; break;
			case ANIMATION_PACMAN: currentAnimName = "Pac-Man"; break;
			case ANIMATION_SNOW: currentAnimName = "Snow Falling"; break;
			case ANIMATION_OCEAN: currentAnimName = "Ocean Waves"; break;
			case ANIMATION_CHAMPAGNE_FIREWORKS: currentAnimName = "Fireworks"; break;
			case ANIMATION_TEMPERATURE: currentAnimName = "Temperature Display"; break;
			case ANIMATION_QIX: currentAnimName = "Qix Lines"; break;
		}
		html += "<p>Current Animation: <strong>" + currentAnimName + "</strong></p>";
		html += "<form action='/setanimation' method='POST'>";
		html += "<label>Select Animation:</label>";
		html += "<div class='radio-group'>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_MATRIX_RAIN ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='0'" + String(currentAnimation == ANIMATION_MATRIX_RAIN ? " checked" : "") + ">Matrix Rain</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_POLISH_FLAG ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='1'" + String(currentAnimation == ANIMATION_POLISH_FLAG ? " checked" : "") + ">Polish Flag</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_UKRAINIAN_FLAG ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='2'" + String(currentAnimation == ANIMATION_UKRAINIAN_FLAG ? " checked" : "") + ">Ukrainian Flag</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_WINDMILL ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='3'" + String(currentAnimation == ANIMATION_WINDMILL ? " checked" : "") + ">Windmill</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_PACMAN ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='4'" + String(currentAnimation == ANIMATION_PACMAN ? " checked" : "") + ">Pac-Man</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_SNOW ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='5'" + String(currentAnimation == ANIMATION_SNOW ? " checked" : "") + ">Snow Falling</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_OCEAN ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='6'" + String(currentAnimation == ANIMATION_OCEAN ? " checked" : "") + ">Ocean Waves</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_CHAMPAGNE_FIREWORKS ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='7'" + String(currentAnimation == ANIMATION_CHAMPAGNE_FIREWORKS ? " checked" : "") + ">Fireworks</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_TEMPERATURE ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='8'" + String(currentAnimation == ANIMATION_TEMPERATURE ? " checked" : "") + ">Temperature Display</label>";
		html += "</div>";
		html += "<div class='radio-option" + String(currentAnimation == ANIMATION_QIX ? " selected" : "") + "'>";
		html += "<label><input type='radio' name='animation' value='9'" + String(currentAnimation == ANIMATION_QIX ? " checked" : "") + ">Qix Lines</label>";
		html += "</div>";
		html += "</div>";
		html += "<input type='submit' value='Change Animation'>";
		html += "</form>";
		html += "<h3>Navigation</h3>";
		html += "<p><a href='/matrix'>Matrix Visualizer</a></p>";
		html += "<p><a href='/status'>System Status</a></p>";
		if (wifiConnected)
		{
			html += "<p><a href='/sensor'>Sensor Data</a></p>";
		}
		html += "<p><a href='/admin'>Admin Settings</a></p>";
	}

	html += "</div></body></html>";
	server.send(200, "text/html", html);
}

void handleStatus()
{
	String html = "<!DOCTYPE html><html><head>";
	html += "<meta charset='UTF-8'>";
	html += "<title>S3Serpentine System Status</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
	html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:linear-gradient(135deg, #4facfe 0%, #00f2fe 25%, #03dac6 50%, #018786 75%, #004d40 100%);min-height:100vh;background-attachment:fixed}";
	html += ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
	html += ".status-item{margin:10px 0;padding:10px;background:#f9f9f9;border-radius:5px;border-left:4px solid #4CAF50}";
	html += ".status-disconnected{border-left-color:#f44336}</style></head><body>";
	html += "<div class='container'><h2>System Status</h2>";
	
	html += "<h3>Network Information</h3>";
	html += "<div class='status-item" + String(!wifiConnected ? " status-disconnected" : "") + "'>";
	html += "<strong>WiFi Status:</strong> " + String(wifiConnected ? "Connected" : "Disconnected");
	html += "</div>";
	
	html += "<div class='status-item'>";
	html += "<strong>MAC Address:</strong> " + WiFi.macAddress();
	html += "</div>";
	
	if (wifiConnected)
	{
		html += "<div class='status-item'>";
		html += "<strong>Network Name:</strong> " + savedSSID;
		html += "</div>";
		
		html += "<div class='status-item'>";
		html += "<strong>IP Address:</strong> " + WiFi.localIP().toString();
		html += "</div>";
		
		html += "<div class='status-item'>";
		html += "<strong>Signal Strength:</strong> " + String(WiFi.RSSI()) + " dBm";
		html += "</div>";
	}
	
	html += "<h3>Hardware Information</h3>";
	html += "<div class='status-item'>";
	html += "<strong>LED Matrix:</strong> " + String(matrixCols) + "x" + String(matrixRows) + " (" + String(ledStripNumpixels) + " LEDs)";
	html += "</div>";
	
	html += "<div class='status-item" + String(!ahtSensorAvailable ? " status-disconnected" : "") + "'>";
	html += "<strong>Temperature Sensor:</strong> " + String(ahtSensorAvailable ? "AHT10 Connected" : "Not Found");
	html += "</div>";
	
	if (ahtSensorAvailable)
	{
		html += "<div class='status-item'>";
		html += "<strong>Current Temperature:</strong> " + String(currentTemperature, 1) + "°C";
		html += "</div>";
		
		html += "<div class='status-item'>";
		html += "<strong>Current Humidity:</strong> " + String(currentHumidity, 1) + "%";
		html += "</div>";
	}
	
	html += "<h3>System Information</h3>";
	html += "<div class='status-item'>";
	html += "<strong>Uptime:</strong> " + String(millis() / 1000) + " seconds";
	html += "</div>";
	
	html += "<div class='status-item'>";
	html += "<strong>Free Memory:</strong> " + String(ESP.getFreeHeap()) + " bytes";
	html += "</div>";
	
	html += "<p><a href='/'>← Back to Main Page</a></p>";
	html += "</div></body></html>";
	server.send(200, "text/html", html);
}

void handleMatrix()
{
	String html = "<!DOCTYPE html><html><head>";
	html += "<meta charset='UTF-8'>";
	html += "<title>S3Serpentine Matrix Visualizer</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
	html += "<style>";
	html += "body{font-family:Arial,sans-serif;margin:20px;background:linear-gradient(135deg, #4facfe 0%, #00f2fe 25%, #03dac6 50%, #018786 75%, #004d40 100%);min-height:100vh;background-attachment:fixed}";
	html += ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
	html += ".matrix-grid{display:grid;grid-template-columns:repeat(" + String(matrixCols) + ", 1fr);gap:2px;margin:20px 0;background:#333;padding:10px;border-radius:8px}";
	html += ".led{aspect-ratio:1;background:#000;border-radius:3px;transition:background-color 0.3s ease}";
	html += ".controls{margin:20px 0;text-align:center}.refresh-btn{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer}";
	html += ".refresh-btn:hover{background:#45a049}.info{background:#f9f9f9;padding:15px;border-radius:5px;margin:10px 0}";
	html += "</style>";
	html += "<script>";
	html += "let autoRefresh = true;";
	html += "async function updateMatrix() {";
	html += "  try {";
	html += "    const response = await fetch('/matrixdata');";
	html += "    const data = await response.json();";
	html += "    data.leds.forEach((color, index) => {";
	html += "      const led = document.querySelector(`.led[data-pixel='${index}']`);";
	html += "      if (led) {";
	html += "        const r = (color >> 16) & 0xFF;";
	html += "        const g = (color >> 8) & 0xFF;";
	html += "        const b = color & 0xFF;";
	html += "        led.style.backgroundColor = `rgb(${r},${g},${b})`;";
	html += "      }";
	html += "    });";
	html += "    document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();";
	html += "  } catch (e) { console.error('Update failed:', e); }";
	html += "}";
	html += "function toggleAutoRefresh() {";
	html += "  autoRefresh = !autoRefresh;";
	html += "  document.getElementById('autoBtn').textContent = autoRefresh ? 'Stop Auto-Refresh' : 'Start Auto-Refresh';";
	html += "}";
	html += "setInterval(() => { if (autoRefresh) updateMatrix(); }, 1500);";
	html += "window.onload = updateMatrix;";
	html += "</script>";
	html += "</head><body>";
	html += "<div class='container'>";
	html += "<h2>LED Matrix Visualizer</h2>";
	
	html += "<div class='info'>";
	html += "<strong>Matrix Configuration:</strong> " + String(matrixCols) + "×" + String(matrixRows) + " (" + String(ledStripNumpixels) + " LEDs)<br>";
	html += "<strong>Layout:</strong> Serpentine (zig-zag) wiring<br>";
	html += "<strong>Last Update:</strong> <span id='lastUpdate'>-</span>";
	html += "</div>";
	
	html += "<div class='matrix-grid'>";
	// Create LED grid elements
	for (int row = 0; row < matrixRows; row++) {
		for (int col = 0; col < matrixCols; col++) {
			int pixelIdx = pixelIndex(col, row);
			html += "<div class='led' data-pixel='" + String(pixelIdx) + "' title='LED " + String(pixelIdx) + " (" + String(col) + "," + String(row) + ")'></div>";
		}
	}
	html += "</div>";
	
	html += "<div class='controls'>";
	html += "<button class='refresh-btn' onclick='updateMatrix()'>Refresh Now</button>";
	html += "<button id='autoBtn' class='refresh-btn' onclick='toggleAutoRefresh()' style='margin-left:10px'>Stop Auto-Refresh</button>";
	html += "</div>";
	
	html += "<p><a href='/'>← Back to Main Page</a></p>";
	html += "</div></body></html>";
	server.send(200, "text/html", html);
}

void handleMatrixData()
{
	String json = "{\"leds\":[";
	for (int i = 0; i < ledStripNumpixels; i++) {
		uint32_t color = myLedStrip.getPixelColor(i);
		if (i > 0) json += ",";
		json += String(color);
	}
	json += "],\"timestamp\":" + String(millis()) + ",\"cols\":" + String(matrixCols) + ",\"rows\":" + String(matrixRows) + "}";
	server.send(200, "application/json", json);
}

void handleAdmin()
{
	String html = "<!DOCTYPE html><html><head>";
	html += "<meta charset='UTF-8'>";
	html += "<title>S3Serpentine Admin Settings</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
	html += "<style>body{font-family:Arial,sans-serif;margin:40px;background:linear-gradient(135deg, #4facfe 0%, #00f2fe 25%, #03dac6 50%, #018786 75%, #004d40 100%);min-height:100vh;background-attachment:fixed}";
	html += ".container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
	html += "input[type=text],input[type=password],select{width:100%;padding:10px;margin:10px 0;box-sizing:border-box;border:1px solid #ddd;border-radius:5px}";
	html += "input[type=submit]{background:#4CAF50;color:white;padding:12px 20px;border:none;border-radius:5px;cursor:pointer;width:100%}";
	html += "input[type=submit]:hover{background:#45a049}";
	html += ".danger{background:#f44336;color:white;padding:12px 20px;text-decoration:none;border-radius:5px;display:inline-block;margin:5px 0;text-align:center}";
	html += ".danger:hover{background:#d32f2f}</style></head><body>";
	html += "<div class='container'><h2>Admin Settings</h2>";
	
	html += "<h3>LED Strip Configuration</h3>";
	html += "<form action='/saveneopixel' method='POST'>";
	html += "<label for='pixeltype'>LED Strip Type:</label>";
	html += "<select id='pixeltype' name='pixeltype' required>";
	html += "<option value='0'";
	if (currentNeoPixelType == NEOPIXEL_GRB) html += " selected";
	html += ">NEO_GRB + NEO_KHZ800 (Default)</option>";
	html += "<option value='1'";
	if (currentNeoPixelType == NEOPIXEL_BGR) html += " selected";
	html += ">NEO_BGR + NEO_KHZ800</option>";
	html += "<option value='2'";
	if (currentNeoPixelType == NEOPIXEL_RGB) html += " selected";
	html += ">NEO_RGB + NEO_KHZ800</option>";
	html += "<option value='3'";
	if (currentNeoPixelType == NEOPIXEL_WRGB) html += " selected";
	html += ">NEO_WRGB + NEO_KHZ800</option>";
	html += "</select>";
	html += "<p>Current: " + getNeoPixelTypeName(currentNeoPixelType) + "</p>";
	html += "<input type='submit' value='Update LED Strip Type'>";
	html += "</form>";
	
	html += "<h3>OTA Configuration</h3>";
	html += "<form action='/saveota' method='POST'>";
	html += "<label for='otapassword'>New OTA Password:</label>";
	html += "<input type='text' id='otapassword' name='otapassword' value='' required>";
	html += "<input type='submit' value='Update OTA Password'>";
	html += "</form>";
	html += "<p><a href='/resetota' class='danger' style='width:100%;box-sizing:border-box'>Reset OTA Password to Default</a></p>";
	
	html += "<h3>System Settings</h3>";
	html += "<p><a href='/reset' class='danger' style='width:100%;box-sizing:border-box'>Reset WiFi Settings & Restart</a></p>";
	
	html += "<p><a href='/'>Main Page</a></p>";
	html += "</div></body></html>";
	server.send(200, "text/html", html);
}

void handleSetAnimation()
{
	if (server.hasArg("animation"))
	{
		int animationIndex = server.arg("animation").toInt();
		
		if (animationIndex >= 0 && animationIndex <= 10)
		{
			currentAnimation = (AnimationMode)animationIndex;
			
			// Initialize the new animation
			switch(currentAnimation) {
				case ANIMATION_ROTATING_HEXAGONS:
					initRotatingHexagonsAnimation();
					Serial.println("Animation changed to Rotating Hexagons");
					break;
				case ANIMATION_MATRIX_RAIN:
					initMatrixRainAnimation();
					Serial.println("Animation changed to Matrix Rain");
					break;
				case ANIMATION_POLISH_FLAG:
					initFlagAnimations();
					Serial.println("Animation changed to Polish Flag");
					break;
				case ANIMATION_UKRAINIAN_FLAG:
					initFlagAnimations();
					Serial.println("Animation changed to Ukrainian Flag");
					break;
				case ANIMATION_WINDMILL:
					initWindmillAnimation();
					Serial.println("Animation changed to Windmill");
					break;
				case ANIMATION_PACMAN:
					initPacmanAnimation();
					Serial.println("Animation changed to Pac-Man");
					break;
				case ANIMATION_SNOW:
					initSnowAnimation();
					Serial.println("Animation changed to Snow");
					break;
				case ANIMATION_OCEAN:
					Serial.println("Animation changed to Ocean");
					break;
				case ANIMATION_CHAMPAGNE_FIREWORKS:
					initFireworksAnimation();
					Serial.println("Animation changed to Fireworks");
					break;
				case ANIMATION_TEMPERATURE:
					Serial.println("Animation changed to Temperature Display");
					break;
				case ANIMATION_QIX:
					initQixAnimation();
					Serial.println("Animation changed to Qix Lines");
					break;
			}
			
			// Reset animation timer
			lastAnimationChange = millis();
			
			String html = "<!DOCTYPE html><html><head>";
			html += "<meta charset='UTF-8'>";
			html += "<title>Animation Changed</title>";
			html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
			html += "<meta http-equiv='refresh' content='2;url=/'>";
			html += "</head><body>";
			html += "<h2>Animation Changed!</h2>";
			html += "<p>The LED animation has been updated successfully.</p>";
			html += "<p>You will be redirected automatically...</p>";
			html += "</body></html>";
			server.send(200, "text/html", html);
		}
		else
		{
			server.send(400, "text/plain", "Invalid animation index");
		}
	}
	else
	{
		server.send(400, "text/plain", "Missing animation parameter");
	}
}

void handleSave()
{
	if (server.hasArg("ssid"))
	{
		String ssid = server.arg("ssid");
		String password = server.arg("password");

		saveWiFiCredentials(ssid, password);

		String html = "<!DOCTYPE html><html><head>";
		html += "<meta charset='UTF-8'>";
		html += "<title>S3Serpentine WiFi Setup</title>";
		html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
		html += "<meta http-equiv='refresh' content='10;url=/'>";
		html += "</head><body>";
		html += "<h2>Settings Saved!</h2>";
		html += "<p>WiFi credentials have been saved. The device will restart and connect to your network.</p>";
		html += "<p>You will be redirected automatically...</p>";
		html += "</body></html>";

		server.send(200, "text/html", html);

		delay(2000);
		ESP.restart();
	}
	else
	{
		server.send(400, "text/plain", "Missing SSID parameter");
	}
}

void handleSensorData()
{
	unsigned long secondSinceLastRead = (millis() - lastTemperatureReadTime) / 1000;
	String json = "{";
	json += "\"temperature\":" + String(currentTemperature, 2) + ",";
	json += "\"humidity\":" + String(currentHumidity, 2) + ",";
	json += "\"timestamp\":" + String(millis()) + ",";
	json += "\"sslr\":" + String(secondSinceLastRead);
	json += "}";

	server.send(200, "application/json", json);
}

void handleSaveOTA()
{
	if (server.hasArg("otapassword"))
	{
		String otaPassword = server.arg("otapassword");

		saveOTAPassword(otaPassword);

		String html = "<!DOCTYPE html><html><head>";
		html += "<meta charset='UTF-8'>";
		html += "<title>S3Serpentine OTA Setup</title>";
		html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
		html += "<meta http-equiv='refresh' content='3;url=/'>";
		html += "</head><body>";
		html += "<h2>OTA Password Updated!</h2>";
		html += "<p>OTA password has been saved. The change takes effect immediately.</p>";
		html += "<p>You will be redirected automatically...</p>";
		html += "</body></html>";

		server.send(200, "text/html", html);
	}
	else
	{
		server.send(400, "text/plain", "Missing OTA password parameter");
	}
}

void handleResetOTA()
{
	resetOTAPassword();

	String html = "<!DOCTYPE html><html><head>";
	html += "<meta charset='UTF-8'>";
	html += "<title>S3Serpentine OTA Setup</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
	html += "<meta http-equiv='refresh' content='3;url=/'>";
	html += "</head><body>";
	html += "<h2>OTA Password Reset!</h2>";
	html += "<p>OTA password has been reset to default. The change takes effect immediately.</p>";
	html += "</body></html>";

	server.send(200, "text/html", html);
}

void handleSaveNeoPixel()
{
	if (server.hasArg("pixeltype"))
	{
		int pixelTypeIndex = server.arg("pixeltype").toInt();
		
		if (pixelTypeIndex >= 0 && pixelTypeIndex <= 3)
		{
			NeoPixelType newType = (NeoPixelType)pixelTypeIndex;
			saveNeoPixelType(newType);
			
			// Reinitialize LED strip with new configuration
			myLedStrip.updateType(currentPixelConfig);
			myLedStrip.begin();
			myLedStrip.clear();
			myLedStrip.show();

			String html = "<!DOCTYPE html><html><head>";
			html += "<meta charset='UTF-8'>";
			html += "<title>LED Strip Type Updated</title>";
			html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
			html += "<meta http-equiv='refresh' content='3;url=/admin'>";
			html += "</head><body>";
			html += "<h2>LED Strip Type Updated!</h2>";
			html += "<p>LED strip type has been changed to: " + getNeoPixelTypeName(newType) + "</p>";
			html += "<p>The change takes effect immediately.</p>";
			html += "<p>You will be redirected automatically...</p>";
			html += "</body></html>";

			server.send(200, "text/html", html);
		}
		else
		{
			server.send(400, "text/plain", "Invalid pixel type index");
		}
	}
	else
	{
		server.send(400, "text/plain", "Missing pixel type parameter");
	}
}

void handleReset()
{
	clearWiFiCredentials();

	String html = "<!DOCTYPE html><html><head>";
	html += "<meta charset='UTF-8'>";
	html += "<title>S3Serpentine WiFi Setup</title>";
	html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
	html += "<meta http-equiv='refresh' content='3;url=/'>";
	html += "</head><body>";
	html += "<h2>WiFi Settings Reset!</h2>";
	html += "<p>WiFi credentials have been cleared. The device will restart in configuration mode.</p>";
	html += "</body></html>";

	server.send(200, "text/html", html);

	delay(2000);
	ESP.restart();
}

void handleFavicon()
{
	// Simple 16x16 favicon.ico (minimal size)
	// This is a tiny 1-bit black icon to avoid 404 errors
	const uint8_t favicon[] PROGMEM = {
		0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x30, 0x00,
		0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};
	server.send_P(200, "image/x-icon", (const char*)favicon, sizeof(favicon));
}

void handleNotFound()
{
	// Redirect all unknown requests to root (captive portal behavior)
	server.sendHeader("Location", "/", true);
	server.send(302, "text/plain", "");
}

void setupWebServer()
{
	server.on("/", handleRoot);
	server.on("/matrix", handleMatrix);
	server.on("/matrixdata", handleMatrixData);
	server.on("/status", handleStatus);
	server.on("/admin", handleAdmin);
	server.on("/setanimation", HTTP_POST, handleSetAnimation);
	server.on("/save", HTTP_POST, handleSave);
	server.on("/saveota", HTTP_POST, handleSaveOTA);
	server.on("/saveneopixel", HTTP_POST, handleSaveNeoPixel);
	server.on("/sensor", handleSensorData);
	server.on("/reset", handleReset);
	server.on("/resetota", handleResetOTA);
	server.on("/favicon.ico", handleFavicon);
	server.onNotFound(handleNotFound);

	server.begin();
	Serial.println("Web server started");
}

void setupOTA()
{
	// Configure OTA hostname and password
	ArduinoOTA.setHostname("s3Serpentine");
	ArduinoOTA.setPassword(savedOTAPassword.c_str());

	ArduinoOTA.onStart([]()
					   {
		String type;
		if (ArduinoOTA.getCommand() == U_FLASH) {
			type = "sketch";
		} else { // U_SPIFFS
			type = "filesystem";
		}
		Serial.println("Start updating " + type); });

	ArduinoOTA.onEnd([]()
					 { Serial.println("\nEnd"); });

	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
						  {
			if (millis() - last_ota_time > 500) {
					unsigned int progressPercent = (progress / (total / 100));
					if (progressPercent != previousUploadProgressPercent) {
						Serial.printf("Progress: %u%%\r", progressPercent);	
						last_ota_time = millis();
					}	
					
					previousUploadProgressPercent = progressPercent; 
			} });

	ArduinoOTA.onError([](ota_error_t error)
					   {
						   Serial.printf("OTA Error[%u]: ", error);
						   if (error == OTA_AUTH_ERROR)
						   {
							   Serial.println("Auth Failed");
						   }
						   else if (error == OTA_BEGIN_ERROR)
						   {
							   Serial.println("Begin Failed");
						   }
						   else if (error == OTA_CONNECT_ERROR)
						   {
							   Serial.println("Connect Failed");
						   }
						   else if (error == OTA_RECEIVE_ERROR)
						   {
							   Serial.println("Receive Failed");
						   }
						   else if (error == OTA_END_ERROR)
						   {
							   Serial.println("End Failed");
						   } });

	ArduinoOTA.begin();
	Serial.println("OTA Ready");
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
}

// Compile-time matrix validation (will show warnings/errors at compile time)
static_assert(matrixCols >= MIN_COLS && matrixCols <= MAX_COLS,
			  "Matrix columns must be between MIN_COLS and MAX_COLS");
static_assert(matrixRows >= MIN_ROWS && matrixRows <= MAX_ROWS,
			  "Matrix rows must be between MIN_ROWS and MAX_ROWS");
static_assert(ledStripNumpixels <= MAX_LED_COUNT,
			  "Total LED count exceeds MAX_LED_COUNT");

void handleButtonPress()
{
	int reading = digitalRead(BUTTON_PIN);

	if (reading != lastButtonState)
	{
		lastDebounceTime = millis();
	}

	if ((millis() - lastDebounceTime) > debounceDelay)
	{
		if (reading != buttonState)
		{
			buttonState = reading;

			if (buttonState == LOW && !buttonPressed)
			{
				// Button just pressed
				buttonPressed = true;
				buttonPressStartTime = millis();
				Serial.println("Button pressed");
			}
			else if (buttonState == HIGH && buttonPressed)
			{
				// Button just released
				buttonPressed = false;
				unsigned long pressDuration = millis() - buttonPressStartTime;

				if (pressDuration >= LONG_PRESS_DURATION)
				{
					// Long press detected - enter WiFi config mode
					Serial.println("Long press detected - entering WiFi configuration mode");
					clearWiFiCredentials();
					ESP.restart();
				}
				else
				{
					// Short press - toggle LED or other functionality
					Serial.println("Short press detected");
				}
			}
		}
	}

	lastButtonState = reading;
}

void setup()
{
	Serial.begin(115200);
	pinMode(RGB_BUILTIN, OUTPUT);

	// Initialize builtin RGB
	setBuiltinRGB(1, 0, 0);
	sleep(5);

	// Load NeoPixel type configuration
	loadNeoPixelType();

	// Initialize LED strip with loaded configuration
	myLedStrip.updateType(currentPixelConfig);
	myLedStrip.begin();
	myLedStrip.clear();
	myLedStrip.show();

	// Initialize animations
	initRotatingHexagonsAnimation();
	initMatrixRainAnimation();
	initFlagAnimations();
	initWindmillAnimation();
	initPacmanAnimation();
	initSnowAnimation();
	initFireworksAnimation();
	initQixAnimation();
	lastAnimationChange = millis();

	Serial.printf("LED Matrix initialized: %dx%d (%d LEDs)\n", matrixCols, matrixRows, ledStripNumpixels);

	lastDebounceTime = millis();
	Serial.println("Booting");

	pinMode(BUTTON_PIN, INPUT);
	pinMode(ledStripPin, OUTPUT);

	Serial.println("LED strip initialized");

	// Initialize I2C for AHT10 sensor
	Wire.begin(AHT_SDA_PIN, AHT_SCL_PIN);
	delay(100);

	// Initialize AHT10 temperature sensor
	if (aht.begin())
	{
		ahtSensorAvailable = true;
		Serial.println("AHT10 temperature sensor initialized successfully");
	}
	else
	{
		ahtSensorAvailable = false;
		Serial.println("AHT10 temperature sensor not found.");
	}

	// Load WiFi credentials and OTA password from preferences
	loadWiFiCredentials();
	loadOTAPassword();

	// Try to connect to saved WiFi network
	if (savedSSID.length() > 0)
	{
		do
		{
			Serial.println("Attempting to connect to saved WiFi network...");
			wifiConnected = connectToWiFi();
		} while (!wifiConnected);
		if (wifiConnected)
		{
			// WiFi connected successfully - start web server and OTA
			WiFi.mode(WIFI_STA);
			setupWebServer();
			setupOTA();
			Serial.println("WiFi connected - sensor data available at /sensor");
		}
	}
	else
	{
		Serial.println("No WiFi credentials found - starting configuration mode");
		startAccessPoint();
		setupWebServer();
	}

	pinMode(RGB_BUILTIN, OUTPUT);
	Serial.println("Setup complete - starting main loop");
}

void loop()
{
	// Handle web server requests
	server.handleClient();

	// Handle OTA updates (only when WiFi is connected)
	if (wifiConnected && !wifiConfigMode)
	{
		ArduinoOTA.handle();
	}

	// Handle DNS requests in AP mode (for captive portal)
	if (wifiConfigMode)
	{
		dnsServer.processNextRequest();
	}

	// Handle button presses
	handleButtonPress();

	// Animation system - alternate every 30 seconds
	if (millis() - lastAnimationChange > ANIMATION_DURATION)
	{
		// Cycle through all animations starting with Rotating Hexagons
		switch(currentAnimation) {
			case ANIMATION_ROTATING_HEXAGONS:
				currentAnimation = ANIMATION_MATRIX_RAIN;
				initMatrixRainAnimation();
				Serial.println("Switched to Matrix Rain animation");
				break;
			case ANIMATION_MATRIX_RAIN:
				currentAnimation = ANIMATION_POLISH_FLAG;
				initFlagAnimations();
				Serial.println("Switched to Polish Flag animation");
				break;
			case ANIMATION_POLISH_FLAG:
				currentAnimation = ANIMATION_UKRAINIAN_FLAG;
				initFlagAnimations();
				Serial.println("Switched to Ukrainian Flag animation");
				break;
			case ANIMATION_UKRAINIAN_FLAG:
				currentAnimation = ANIMATION_WINDMILL;
				initWindmillAnimation();
				Serial.println("Switched to Windmill animation");
				break;
			case ANIMATION_WINDMILL:
				currentAnimation = ANIMATION_PACMAN;
				initPacmanAnimation();
				Serial.println("Switched to Pac-Man animation");
				break;
			case ANIMATION_PACMAN:
				currentAnimation = ANIMATION_SNOW;
				initSnowAnimation();
				Serial.println("Switched to Snow animation");
				break;
			case ANIMATION_SNOW:
				currentAnimation = ANIMATION_OCEAN;
				Serial.println("Switched to Ocean animation");
				break;
			case ANIMATION_OCEAN:
				currentAnimation = ANIMATION_CHAMPAGNE_FIREWORKS;
				initFireworksAnimation();
				Serial.println("Switched to Fireworks animation");
				break;
			case ANIMATION_CHAMPAGNE_FIREWORKS:
				currentAnimation = ANIMATION_TEMPERATURE;
				Serial.println("Switched to Temperature Display animation");
				break;
			case ANIMATION_TEMPERATURE:
				currentAnimation = ANIMATION_QIX;
				initQixAnimation();
				Serial.println("Switched to Qix animation");
				break;
			case ANIMATION_QIX:
				currentAnimation = ANIMATION_ROTATING_HEXAGONS;
				initRotatingHexagonsAnimation();
				Serial.println("Switched to Rotating Hexagons animation");
				break;
		}
		lastAnimationChange = millis();
	}

	// Run current animation
	switch(currentAnimation) {
		case ANIMATION_ROTATING_HEXAGONS:
			updateRotatingHexagonsAnimation();
			break;
		case ANIMATION_MATRIX_RAIN:
			updateMatrixRainAnimation();
			break;
		case ANIMATION_POLISH_FLAG:
			updatePolishFlagAnimation();
			break;
		case ANIMATION_UKRAINIAN_FLAG:
			updateUkrainianFlagAnimation();
			break;
		case ANIMATION_WINDMILL:
			updateWindmillAnimation();
			break;
		case ANIMATION_PACMAN:
			updatePacmanAnimation();
			break;
		case ANIMATION_SNOW:
			updateSnowAnimation();
			break;
		case ANIMATION_OCEAN:
			updateOceanAnimation();
			break;
		case ANIMATION_CHAMPAGNE_FIREWORKS:
			updateFireworksAnimation();
			break;
		case ANIMATION_TEMPERATURE:
			updateTemperatureAnimation();
			break;
		case ANIMATION_QIX:
			updateQixAnimation();
			break;
	}

	// Read temperature sensor if available
	if (ahtSensorAvailable)
	{
		if (millis() - lastTemperatureReadTime > 2000)
		{
			sensors_event_t humidity, temp;
			if (aht.getEvent(&humidity, &temp))
			{
				float temperature = temp.temperature;

				currentTemperature = temperature;			  // Update global variable
				currentHumidity = humidity.relative_humidity; // Update global humidity
				lastTemperatureReadTime = millis();
				// Only print to serial every few seconds to avoid spam
				static unsigned long lastPrint = 0;
				if (millis() - lastPrint > 5000)
				{
					Serial.print("Temperature: ");
					Serial.print(temperature);
					Serial.print(" °C, Humidity: ");
					Serial.print(humidity.relative_humidity);
					Serial.println(" %");
					lastPrint = millis();
				}
			}
			else
			{
				Serial.println("Failed to read from AHT10 sensor");
			}
		}
	}
	else
	{
		//Serial.println("No Temperature");
	}

	// Small delay for responsiveness
	delay(1);
}