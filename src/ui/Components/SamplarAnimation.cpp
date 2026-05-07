#include "SamplarAnimation.h"
#include "../../utils/DataPaths.h"

using namespace juce;

namespace samplar {

SamplarAnimation::SamplarAnimation()
{
    // Make animation much more visible and faster
    animationSpeed_ = 3.0f;  // 3x faster movement
    monsterSize_ = 80;       // Much bigger monster

    // Load available background images
    loadBackgroundFiles();
    selectRandomBackground();

    // Spooky messages moved to ProcessingPage center - removed from here

    // Don't start animation automatically - let ProcessingPage control when it starts
    // startAnimation(); // Removed - controlled by ProcessingPage
}

SamplarAnimation::~SamplarAnimation()
{
    stopAnimation();
}

void SamplarAnimation::paint(Graphics& g)
{
    // Draw stretching background image first (behind everything)
    drawBackground(g);
    
    // Draw the animated SAMPLAR
    drawMonstaAnimation(g);

    // Spooky messages moved to ProcessingPage center

    // Draw processing status if active
    if (processingActive_)
    {
        drawProcessingStatus(g);
    }
}

void SamplarAnimation::resized()
{
    // Adjust monster position to stay within bounds
    float width = (float)getWidth();
    if (monstaX_ > width - 60.0f)
        monstaX_ = width - 60.0f;
    if (monstaX_ < 60.0f)
        monstaX_ = 60.0f;
}

void SamplarAnimation::startAnimation()
{
    // 60fps for smooth animation (16.67ms intervals)
    startTimerHz(60);
}

void SamplarAnimation::stopAnimation()
{
    stopTimer();
}

void SamplarAnimation::timerCallback()
{
    // PROCESS-DRIVEN animation - creature position represents loading progress
    float screenWidth = (float)getWidth();
    float edgeBuffer = 54.0f;
    float availableWidth = screenWidth - (2.0f * edgeBuffer);
    
    if (processingActive_ && processingProgress_ >= 0.0f)
    {
        // Creature position follows processing progress, but 80% creature position = 100% completion
        // So we scale progress to only use 80% of available width
        float scaledProgress = processingProgress_ * 0.8f; // Scale to 80% of width
        monstaX_ = edgeBuffer + (availableWidth * scaledProgress);
        
        // When processing is complete (100%), creature reaches 80% of the screen
        if (processingProgress_ >= 1.0f)
        {
            monstaX_ = edgeBuffer + (availableWidth * 0.8f);
        }
    }
    else
    {
        // When not processing, do gentle idle movement around starting position
        static float idleTime = 0.0f;
        idleTime += 0.05f * animationSpeed_;
        monstaX_ = edgeBuffer + 20.0f + (10.0f * std::sin(idleTime));
    }
    
    // Animate mouth opening/closing (eating process) - much slower (10% speed)
    monstaMouthAngle_ += 1.5f * animationSpeed_; // 10% of original 15.0f speed
    if (monstaMouthAngle_ > 60.0f)
    {
        monstaMouthAngle_ = 0.0f;
    }
    
    // Animate eye blinking independently
    monstaEyeBlink_ += (int)(1 * animationSpeed_);
    if (monstaEyeBlink_ > 50)
    {
        monstaEyeBlink_ = 0;
    }
    
    // Force complete screen repaint every animation frame
    repaint();
}

void SamplarAnimation::drawMonstaAnimation(Graphics& g)
{
    // Position in center vertically
    float yPos = (float)getHeight() / 2.0f;
    float monstaSize = (float)monsterSize_ * 1.0f; // Normal size

    // Draw SAMPLAR body - EXACTLY like Python version
    juce::Colour reddishOrange(220, 80, 40);  // Reddish orange monster color
    juce::Colour darkOrange(180, 60, 30);     // Darker outline

    g.setColour(reddishOrange);

    // Main body - rounded rectangle
    Rectangle<float> bodyRect(
        monstaX_ - monstaSize / 2.0f,
        yPos - monstaSize / 2.0f,
        monstaSize,
        monstaSize - 5.0f
    );

    g.fillRoundedRectangle(bodyRect, 8.0f);

    // Draw outline
    g.setColour(darkOrange);
    g.drawRoundedRectangle(bodyRect, 8.0f, 2.0f);

    // Debug yellow circle removed - using proper SAMPLAR character now

    // Draw green hair first (behind the body)
    drawMonstaHair(g, bodyRect);

    // Draw monster features
    drawMonstaFeatures(g, bodyRect);
    
    // Draw audio waveform trail (represents audio samples)
    if (showAudioTrail_)
    {
        drawAudioTrail(g, yPos);
    }
}

void SamplarAnimation::drawMonstaHair(Graphics& g, const Rectangle<float>& bodyRect)
{
    // Draw green hair spikes pointing DOWN into the face (flipped)
    g.setColour(hairColor_);
    
    float startX = bodyRect.getX();
    float endX = bodyRect.getRight();
    float hairY = bodyRect.getY(); // On top line of creature's head
    
    for (float x = startX; x < endX; x += 5.0f)
    {
        Path hairSpike;
        hairSpike.startNewSubPath(x, hairY);
        hairSpike.lineTo(x + 4.0f, hairY);
        hairSpike.lineTo(x + 2.0f, hairY + 10.0f); // Point DOWN into face instead of up (+ instead of -)
        hairSpike.closeSubPath();
        
        g.fillPath(hairSpike);
    }
}

void SamplarAnimation::drawMonstaFeatures(Graphics& g, const Rectangle<float>& bodyRect)
{
    float centerX = bodyRect.getCentreX();
    float centerY = bodyRect.getCentreY();
    
    // Draw evil triangle eyes facing each other (closer to top of head)
    float eyeSize = 25.0f; // Much bigger eyes
    float eyeOffset = 20.0f; // More spacing for huge eyes
    float eyeY = centerY - 20.0f; // Much closer to top of head
    
    // Draw static eyes (no blinking)
    g.setColour(Colours::black); // Black triangle eyes
    
    // Left evil triangle eye (pointing right towards center)
    Path leftEye;
    float leftEyeX = centerX - eyeOffset;
    leftEye.startNewSubPath(leftEyeX - eyeSize/2.0f, eyeY - eyeSize/3.0f);
    leftEye.lineTo(leftEyeX + eyeSize/2.0f, eyeY);
    leftEye.lineTo(leftEyeX - eyeSize/2.0f, eyeY + eyeSize/3.0f);
    leftEye.closeSubPath();
    g.fillPath(leftEye); // Filled with black
    
    // Right evil triangle eye (pointing left towards center)
    Path rightEye;
    float rightEyeX = centerX + eyeOffset;
    rightEye.startNewSubPath(rightEyeX + eyeSize/2.0f, eyeY - eyeSize/3.0f);
    rightEye.lineTo(rightEyeX - eyeSize/2.0f, eyeY);
    rightEye.lineTo(rightEyeX + eyeSize/2.0f, eyeY + eyeSize/3.0f);
    rightEye.closeSubPath();
    g.fillPath(rightEye); // Filled with black
    
    // Draw evil smile mouth with sharp teeth (animated opening/closing)
    drawEvilSmile(g, centerX, centerY, monstaMouthAngle_);
    
    // Draw small horns/spikes on top
    g.setColour(Colours::darkgreen);
    
    // Left horn
    Path leftHorn;
    leftHorn.startNewSubPath(centerX - 8.0f, bodyRect.getY());
    leftHorn.lineTo(centerX - 12.0f, bodyRect.getY() - 8.0f);
    leftHorn.lineTo(centerX - 4.0f, bodyRect.getY() - 2.0f);
    leftHorn.closeSubPath();
    g.fillPath(leftHorn);
    
    // Right horn
    Path rightHorn;
    rightHorn.startNewSubPath(centerX + 8.0f, bodyRect.getY());
    rightHorn.lineTo(centerX + 12.0f, bodyRect.getY() - 8.0f);
    rightHorn.lineTo(centerX + 4.0f, bodyRect.getY() - 2.0f);
    rightHorn.closeSubPath();
    g.fillPath(rightHorn);
}

void SamplarAnimation::drawEvilSmile(Graphics& g, float centerX, float centerY, float mouthAngle)
{
    float mouthWidth = 50.0f; // Even bigger and wider mouth
    float baseMouthHeight = 20.0f; // Base mouth height
    float mouthY = centerY - 5.0f; // Move mouth higher up to align with blue icons flow
    
    // Animate mouth opening/closing based on mouthAngle (0-60 degrees)
    float openingFactor = std::sin(mouthAngle * 3.14159f / 180.0f * 3.0f); // Convert to radians and animate
    float animatedMouthHeight = baseMouthHeight + (openingFactor * 15.0f); // Mouth opens up to 35px tall
    
    // Draw mouth shape (animated opening/closing)
    g.setColour(Colours::black);
    g.fillRect(centerX - mouthWidth/2.0f, mouthY, mouthWidth, animatedMouthHeight);
    
    // Draw upper teeth (smaller as requested)
    g.setColour(Colours::white);
    
    for (int i = -5; i <= 5; ++i) // More teeth across wider mouth
    {
        float upperToothWidth = 3.5f; // Smaller upper teeth
        float upperToothHeight = animatedMouthHeight / 3.0f; // Smaller upper teeth
        float toothX = centerX + i * 4.5f;
        
        Path upperTooth;
        upperTooth.startNewSubPath(toothX, mouthY);
        upperTooth.lineTo(toothX + upperToothWidth/2.0f, mouthY + upperToothHeight);
        upperTooth.lineTo(toothX + upperToothWidth, mouthY);
        upperTooth.closeSubPath();
        
        g.fillPath(upperTooth);
    }
    
    // Draw lower teeth (new addition)
    for (int i = -4; i <= 4; ++i) // Slightly fewer lower teeth
    {
        float lowerToothWidth = 4.0f; // Lower teeth size
        float lowerToothHeight = animatedMouthHeight / 2.5f; // Lower teeth height
        float toothX = centerX + i * 5.0f + 2.5f; // Offset lower teeth slightly
        float lowerToothY = mouthY + animatedMouthHeight; // Bottom of mouth
        
        Path lowerTooth;
        lowerTooth.startNewSubPath(toothX, lowerToothY);
        lowerTooth.lineTo(toothX + lowerToothWidth/2.0f, lowerToothY - lowerToothHeight);
        lowerTooth.lineTo(toothX + lowerToothWidth, lowerToothY);
        lowerTooth.closeSubPath();
        
        g.fillPath(lowerTooth);
    }
}

void SamplarAnimation::drawAudioTrail(Graphics& g, float yPos)
{
    float edgeBuffer = 54.0f;
    float processedWidth = monstaX_ - edgeBuffer; // How much has been "eaten"
    
    // 1. Draw "eaten leftovers" - processed audio fragments left behind by the creature
    if (processedWidth > 10.0f) // Only draw if creature has moved significantly
    {
        float fragmentSpacing = 12.0f;
        float fragmentSize = 4.0f;
        
        // Draw processed fragments behind the creature (leftovers)
        for (float i = edgeBuffer; i < monstaX_ - 20.0f; i += fragmentSpacing)
        {
            // Different types of leftover fragments
            float fragmentAlpha = 0.8f - ((monstaX_ - i) / processedWidth) * 0.6f; // Fade older fragments
            
            // Draw small processed chunks/bits
            g.setColour(juce::Colour(150, 75, 25).withAlpha(fragmentAlpha)); // Brown-orange processed bits
            
            // Random fragment shapes (simulating eaten/processed pieces)
            float fragmentY = yPos + (3.0f * std::sin(i * 0.3f)); // Slight vertical variation
            
            // Draw irregular shaped leftover fragments
            Path fragment;
            float x = i;
            fragment.startNewSubPath(x - fragmentSize/2, fragmentY - fragmentSize/3);
            fragment.lineTo(x + fragmentSize/3, fragmentY - fragmentSize/2);
            fragment.lineTo(x + fragmentSize/2, fragmentY + fragmentSize/4);
            fragment.lineTo(x - fragmentSize/4, fragmentY + fragmentSize/2);
            fragment.closeSubPath();
            g.fillPath(fragment);
            
            // Add smaller particles around main fragments
            if (((int)i) % ((int)(fragmentSpacing * 2)) == 0) // Every other fragment
            {
                g.setColour(juce::Colour(100, 50, 20).withAlpha(fragmentAlpha * 0.6f));
                for (int p = 0; p < 3; p++)
                {
                    float particleX = x + (rand() % 10 - 5);
                    float particleY = fragmentY + (rand() % 6 - 3);
                    g.fillEllipse(particleX, particleY, 2.0f, 2.0f);
                }
            }
        }
        
        // Sparkle effect at eating point (where blue orb used to be)
        float eatZoneX = monstaX_ + 5.0f; // Define eating zone position
        float eatZoneY = yPos - 5.0f;
        
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        for (int s = 0; s < 4; s++)
        {
            float sparkleX = eatZoneX + (rand() % 12 - 6);
            float sparkleY = eatZoneY + (rand() % 12 - 6);
            g.fillEllipse(sparkleX, sparkleY, 1.5f, 1.5f);
        }
    }
    
    // 2. Draw blue audio waveform trail flowing into creature's mouth
    g.setColour(trailColor_); // Cyan/blue color for unprocessed audio
    
    float barSpacing = 15.0f;
    float barWidth = 3.0f;
    float mouthX = monstaX_; // Creature's mouth position
    float mouthY = yPos - 5.0f; // Mouth Y position (updated to match new drawEvilSmile)
    
    // Add animation counter to make the trail animate like before
    static float animationOffset = 0.0f;
    animationOffset += 2.0f * animationSpeed_; // Animate the trail
    
    for (float i = monstaX_ + 40.0f; i < getWidth(); i += barSpacing) // Start closer to creature
    {
        float distanceToMouth = i - mouthX;
        
        // Only draw bars that are far enough from the mouth (not yet eaten)
        if (distanceToMouth > 25.0f)
        {
            // Vary bar height to simulate animated waveform (unprocessed audio data)
            float barHeight = 8.0f + 6.0f * std::sin((i + animationOffset) * 0.1f);
            
            // Make bars curve toward the mouth as they get closer
            float curveFactor = juce::jmax(0.0f, 1.0f - (distanceToMouth / 150.0f));
            float curvedY = yPos + (mouthY - yPos) * curveFactor * curveFactor;
            
            // Fade bars as they approach the mouth (getting eaten)
            float alpha = juce::jmin(1.0f, distanceToMouth / 60.0f);
            g.setColour(trailColor_.withAlpha(alpha));
            
            g.fillRect(
                i - barWidth/2.0f,
                curvedY - barHeight/2.0f,
                barWidth,
                barHeight
            );
        }
    }
    
    // Draw "suction" effect near the mouth - bars being pulled in
    for (float i = monstaX_ + 25.0f; i <= monstaX_ + 40.0f; i += 8.0f)
    {
        float distanceToMouth = i - mouthX;
        float suctionAlpha = 1.0f - (distanceToMouth / 25.0f);
        
        if (suctionAlpha > 0.0f)
        {
            // Create streaking effect of bars being sucked into mouth
            g.setColour(trailColor_.withAlpha(suctionAlpha * 0.7f));
            float streakY = yPos + (mouthY - yPos) * (1.0f - distanceToMouth / 25.0f);
            
            // Draw elongated bars being sucked in
            g.fillRect(i - 1.0f, streakY - 3.0f, 2.0f, 6.0f);
        }
    }
}

void SamplarAnimation::drawProcessingStatus(Graphics& g)
{
    if (processingProgress_ <= 0.0f) return;
    
    // Adjust progress: start at 20%, and 80% progress = 100% display
    float adjustedProgress = 0.2f + (processingProgress_ * 0.6f); // Maps 0-1 to 0.2-0.8
    float displayProgress = (adjustedProgress - 0.2f) / 0.6f; // Maps 0.2-0.8 to 0-1 for display
    
    // Draw floating spooky progress text much higher above creature
    float yPos = (float)getHeight() / 2.0f;
    float centerX = monstaX_;
    float textY = yPos - 80.0f; // Much higher, no contact with body
    
    // Spooky animation variables
    static float spookTime = 0.0f;
    static int lastProgress = -1;
    static float shakeIntensity = 0.0f;
    
    spookTime += 0.1f;
    
    int currentProgress = (int)(displayProgress * 100.0f);
    
    // Trigger shake when progress changes
    if (currentProgress != lastProgress) {
        shakeIntensity = 5.0f;
        lastProgress = currentProgress;
    }
    
    // Decay shake
    if (shakeIntensity > 0.0f) {
        shakeIntensity *= 0.9f;
    }
    
    // Spooky shake offset
    float shakeX = (rand() % 100 / 100.0f - 0.5f) * shakeIntensity;
    float shakeY = (rand() % 100 / 100.0f - 0.5f) * shakeIntensity;
    
    // Pulsing size effect
    float pulseScale = 1.0f + 0.1f * std::sin(spookTime * 2.0f);
    
    // Video game style font for progress percentage
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 24.0f * pulseScale, juce::Font::bold));
    
    juce::String progressText = juce::String(currentProgress) + "%";
    
    // Spooky glow effect with color cycling
    float colorCycle = std::sin(spookTime * 1.5f);
    juce::Colour spookyColor = juce::Colour(0, 255, 255).interpolatedWith(juce::Colour(255, 0, 255), colorCycle * 0.3f);
    
    // Draw with spooky glow and shake
    for (int i = 4; i >= 0; --i)
    {
        float alpha = 0.2f + (i * 0.15f);
        g.setColour(spookyColor.withAlpha(alpha));
        g.drawText(progressText, 
                  centerX - 35.0f - i + shakeX, textY - i + shakeY, 
                  70.0f + (i * 2), 30.0f + (i * 2), 
                  juce::Justification::centred);
    }
}

// drawSpookyMessages method removed - spooky messages moved to ProcessingPage center

void SamplarAnimation::drawBackground(Graphics& g)
{
    if (currentBackground_.isValid())
    {
        // Draw background stretched only in width, maintaining natural height aspect ratio
        auto bounds = getLocalBounds().toFloat();
        
        // Calculate scale to fit width while maintaining aspect ratio
        float scaleX = bounds.getWidth() / (float)currentBackground_.getWidth();
        float scaledHeight = (float)currentBackground_.getHeight() * scaleX;
        
        // Center the image vertically if it's smaller than viewport height
        float yPos = (bounds.getHeight() - scaledHeight) * 0.5f;
        
        juce::Rectangle<float> imageRect(0.0f, yPos, bounds.getWidth(), scaledHeight);
        
        g.drawImage(currentBackground_, imageRect, juce::RectanglePlacement::fillDestination);
    }
    else
    {
        // Fallback to solid color background
        g.fillAll(backgroundColor_);
    }
}

void SamplarAnimation::loadBackgroundFiles()
{
    backgroundFiles_.clear();
    
    // Get the backgrounds directory via centralized DataPaths resolution
    juce::File backgroundsDir = samplar::DataPaths::getBackgroundsDirectory();
    
    if (backgroundsDir.exists() && backgroundsDir.isDirectory())
    {
        auto files = backgroundsDir.findChildFiles(juce::File::findFiles, false, "*.png;*.PNG;*.jpg;*.JPG;*.jpeg;*.JPEG");
        
        for (const auto& file : files)
        {
            backgroundFiles_.push_back(file.getFullPathName());
        }
        
        juce::Logger::writeToLog("[SamplarAnimation] Loaded " + juce::String(backgroundFiles_.size()) + " background files");
        
        for (const auto& filename : backgroundFiles_)
        {
            juce::Logger::writeToLog("[SamplarAnimation] Found background: " + filename);
        }
    }
    else
    {
        juce::Logger::writeToLog("[SamplarAnimation] ERROR: Backgrounds directory not found at: " + backgroundsDir.getFullPathName());
    }
}

void SamplarAnimation::selectRandomBackground()
{
    if (backgroundFiles_.empty())
    {
        juce::Logger::writeToLog("[SamplarAnimation] No background files available");
        currentBackground_ = juce::Image();
        return;
    }
    
    // Select random background
    int randomIndex = juce::Random::getSystemRandom().nextInt(backgroundFiles_.size());
    juce::String selectedFile = backgroundFiles_[randomIndex];
    
    // Load the selected image
    juce::File imageFile(selectedFile);
    currentBackground_ = juce::ImageCache::getFromFile(imageFile);
    
    if (currentBackground_.isValid())
    {
        juce::Logger::writeToLog("[SamplarAnimation] Selected random background: " + imageFile.getFileName());
    }
    else
    {
        juce::Logger::writeToLog("[SamplarAnimation] ERROR: Failed to load background: " + selectedFile);
    }
    
    // Force repaint to show new background
    repaint();
}

// getCurrentSpookyMessage method removed - spooky messages moved to ProcessingPage center

} // namespace samplar
