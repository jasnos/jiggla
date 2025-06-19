document.addEventListener('DOMContentLoaded', function() {
  try {
    // Get common elements present on all pages
    const loadingDiv = document.getElementById('loading');
    const contentDiv = document.getElementById('content');
    const logoutButton = document.getElementById('logout-button');
    
    // Global function to update movement delay from slider
    window.updateMovementDelay = function(sliderValue) {
      const delayInput = document.getElementById('movement-speed');
      if (delayInput) {
        // Formula: 3000ms when slider=0, 1ms when slider=100
        const delayValue = Math.round(3000 - (sliderValue / 100) * 2999);
        console.log(`Global: Slider value ${sliderValue}, setting delay to ${delayValue}`);
        delayInput.value = delayValue;
        
        // Trigger change for auto-save
        const event = new Event('change', { bubbles: true });
        delayInput.dispatchEvent(event);
      }
    };
    
    // Determine which page we're on
    const isJigglerPage = window.location.pathname === '/' || window.location.pathname === '/index.html';
    
    // Only get jiggler-specific elements if we're on the jiggler page
    let jigglerElements = {};
    
    // Check authentication
    async function checkAuth() {
      try {
        console.log("Dashboard: Checking content access...");
        
        // Show content immediately - Basic Auth is handled by the browser
        if (loadingDiv) loadingDiv.style.display = 'none';
        if (contentDiv) contentDiv.style.display = 'block';
          
        // Only initialize jiggler if we're on the jiggler page
        if (isJigglerPage) {
          // Add a small delay to ensure all elements are loaded
          setTimeout(() => {
            try {
              jigglerElements = {
                jigglerEnabledCheckbox: document.getElementById('jiggler-enabled'),
                moveIntervalInput: document.getElementById('move-interval'),
                movementPatternSelect: document.getElementById('movement-pattern'),
                movementSizeInput: document.getElementById('movement-size'),
                movementSpeedInput: document.getElementById('movement-speed'),
                testButton: document.getElementById('test-button'),
                statusDiv: document.getElementById('status'),
                saveStatus: document.getElementById('save-status'),
                sizeValue: document.getElementById('size-value'),
                lastMovement: document.getElementById('last-movement'),
                activityLog: document.getElementById('activity-log'),
                nextMovementCountdown: document.getElementById('next-movement-countdown'),
                randomDelayCheckbox: document.getElementById('random-delay'),
                movementTrailCheckbox: document.getElementById('movement-trail'),
                speedValue: document.getElementById('speed-value')
              };
              
              // Verify all required elements exist for jiggler page
              const requiredElements = ['jigglerEnabledCheckbox', 'moveIntervalInput', 'movementPatternSelect', 
                                      'movementSizeInput', 'movementSpeedInput', 'saveStatus'];
              const missingElements = requiredElements.filter(elem => !jigglerElements[elem]);
              
              if (missingElements.length > 0) {
                console.error("Required form elements not found:", missingElements);
                return;
              }
              
              // Initialize jiggler functionality
              initializeJiggler();
            } catch (error) {
              console.error("Error initializing jiggler elements:", error);
            }
          }, 100);
        }
      } catch (error) {
        console.error("Dashboard initialization error:", error);
      }
    }
    
    // Store device info for countdown
    let deviceInfo = {
      lastMoveTime: 0,
      nextMoveTime: 0,
      uptime: 0,
      jigglerEnabled: true
    };
    
    // Countdown timer interval
    let countdownInterval = null;
    
    // Auto-save timer
    let saveTimer = null;
    
    // Function to initialize all jiggler-specific functionality
    function initializeJiggler() {
      // Update movement size value display
      const movementSizeSlider = document.getElementById('movement-size');
      const sizeValueDisplay = document.getElementById('size-value');
      
      if (movementSizeSlider && sizeValueDisplay) {
        movementSizeSlider.addEventListener('input', () => {
          sizeValueDisplay.textContent = movementSizeSlider.value;
        });
      }
      
      // Direct implementation for movement speed slider
      const slider = document.getElementById('movement-speed-slider');
      const delayInput = document.getElementById('movement-speed');
      
      if (slider && delayInput) {
        console.log("Setting up speed slider controls");
        
        // Calculate delay value from slider position (0-100)
        function updateDelayFromSlider() {
          const sliderValue = parseInt(slider.value);
          // Formula: 3000ms when slider=0, 1ms when slider=100
          const delayValue = Math.round(3000 - (sliderValue / 100) * 2999);
          console.log(`Slider value: ${sliderValue}, setting delay to: ${delayValue}`);
          delayInput.value = delayValue;
          
          // Trigger change for saving
          delayInput.dispatchEvent(new Event('change', { bubbles: true }));
        }
        
        // Set slider position based on delay value
        function updateSliderFromDelay() {
          const delayValue = parseInt(delayInput.value);
          if (!isNaN(delayValue) && delayValue >= 1 && delayValue <= 3000) {
            // Convert delay (3000-1) to slider (0-100)
            const sliderValue = Math.round(((3000 - delayValue) / 2999) * 100);
            console.log(`Delay value: ${delayValue}, setting slider to: ${sliderValue}`);
            slider.value = sliderValue;
          }
        }
        
        // Add event listeners with direct function references
        slider.addEventListener('input', updateDelayFromSlider);
        delayInput.addEventListener('input', updateSliderFromDelay);
        
        // Initialize slider based on current delay value
        updateSliderFromDelay();
      }
      
      // Auto-save function
      function triggerAutoSave() {
        if (!jigglerElements.saveStatus) return;
        
        clearTimeout(saveTimer);
        jigglerElements.saveStatus.innerHTML = '<i class="bi bi-hourglass-split text-warning"></i> Saving...';
        
        // Delay save to avoid too many requests when changing sliders
        saveTimer = setTimeout(() => {
          saveConfig();
        }, 500);
      }
      
      // Setup auto-save for all config controls
      document.querySelectorAll('.config-control').forEach(control => {
        control.addEventListener('change', triggerAutoSave);
        control.addEventListener('input', triggerAutoSave);
      });
      
      // Special handler for jiggler enabled state
      if (jigglerElements.jigglerEnabledCheckbox) {
        jigglerElements.jigglerEnabledCheckbox.addEventListener('change', function() {
          deviceInfo.jigglerEnabled = this.checked;
          updateCountdownDisplay();
        });
      }
      
      // Load initial configuration
      loadConfig();
      
      // Setup periodic checks
      setInterval(checkLastMovement, 5000);
      
      // Add initial success message to log
      addToLog('Successfully initialized');
      
      // Setup event listeners
      if (jigglerElements.testButton) {
        jigglerElements.testButton.addEventListener('click', testMovement);
      }
    }
    
    // Load configuration
    async function loadConfig() {
      try {
        const response = await fetch('/api/config', {
          credentials: 'same-origin'
        });
        
        if (response.ok) {
          const config = await response.json();
          
          if (jigglerElements.jigglerEnabledCheckbox) {
            jigglerElements.jigglerEnabledCheckbox.checked = !!config.jiggler_enabled;
            deviceInfo.jigglerEnabled = !!config.jiggler_enabled;
          }
          if (jigglerElements.movementPatternSelect) {
            // Default to 'linear' if not set or using old circular_movement
            if (config.movement_pattern) {
              jigglerElements.movementPatternSelect.value = config.movement_pattern;
            } else {
              // Handle legacy setting
              jigglerElements.movementPatternSelect.value = config.circular_movement ? 'circular' : 'linear';
            }
          }
          if (jigglerElements.moveIntervalInput) jigglerElements.moveIntervalInput.value = config.move_interval || 240;
          if (jigglerElements.movementSizeInput) {
            // Divide by 2 since we multiply by 2 when saving
            const size = (config.movement_size || Math.max(5, Math.abs(config.movement_x || 5), Math.abs(config.movement_y || 5))) / 2;
            jigglerElements.movementSizeInput.value = size;
            if (jigglerElements.sizeValue) jigglerElements.sizeValue.textContent = size;
          }
          if (jigglerElements.movementSpeedInput) {
            const movementSpeed = config.movement_speed || 500;
            // Set the value directly
            jigglerElements.movementSpeedInput.value = movementSpeed;
            
            console.log("Setting initial movement speed to:", movementSpeed);
            
            // Force an event to update the slider
            setTimeout(() => {
              const event = new Event('input', { bubbles: true });
              jigglerElements.movementSpeedInput.dispatchEvent(event);
            }, 100);
          }
          
          // New features (with defaults if not in config yet)
          if (jigglerElements.randomDelayCheckbox) jigglerElements.randomDelayCheckbox.checked = !!config.random_delay;
          if (jigglerElements.movementTrailCheckbox) jigglerElements.movementTrailCheckbox.checked = !!config.movement_trail;
          
          // Update last movement time
          checkLastMovement();
          
          addToLog('Configuration loaded');
        }
      } catch (error) {
        console.error("Config load error:", error);
        showStatus('Failed to load configuration: ' + error.message, false);
      }
    }
    
    // Get last movement time
    async function checkLastMovement() {
      try {
        if (!jigglerElements.lastMovement) return;
        
        const response = await fetch('/api/status', {
          credentials: 'same-origin'
        });
        
        if (response.ok) {
          const data = await response.json();
          deviceInfo.jigglerEnabled = !!data.jiggler_enabled;
          
          if (data && data.last_move_time) {
            // Get current device uptime in milliseconds
            const uptime = data.uptime_seconds * 1000;
            deviceInfo.uptime = uptime;
            
            // Store last and next move times
            deviceInfo.lastMoveTime = data.last_move_time;
            deviceInfo.nextMoveTime = data.next_move_time;
            // Record time of this check
            deviceInfo.lastCheckTime = new Date().getTime();
            
            // Calculate how long ago the last movement was (in milliseconds)
            const timeSinceLastMove = uptime - data.last_move_time;
            
            // Format the time ago in a human-readable format
            if (timeSinceLastMove < 60000) {
              // Less than a minute
              jigglerElements.lastMovement.textContent = `${Math.floor(timeSinceLastMove / 1000)} seconds ago`;
            } else if (timeSinceLastMove < 3600000) {
              // Less than an hour
              jigglerElements.lastMovement.textContent = `${Math.floor(timeSinceLastMove / 60000)} minutes ago`;
            } else {
              // Hours and minutes
              const hours = Math.floor(timeSinceLastMove / 3600000);
              const minutes = Math.floor((timeSinceLastMove % 3600000) / 60000);
              jigglerElements.lastMovement.textContent = `${hours}h ${minutes}m ago`;
            }
            
            // Start or update countdown timer
            updateCountdownTimer();
          } else {
            jigglerElements.lastMovement.textContent = "No movements yet";
            if (jigglerElements.nextMovementCountdown) {
              jigglerElements.nextMovementCountdown.textContent = deviceInfo.jigglerEnabled ? "Waiting for first movement" : "Jiggler is disabled";
            }
          }
        }
      } catch (error) {
        console.error("Status check error:", error);
      }
    }
    
    // Update countdown timer
    function updateCountdownTimer() {
      if (!jigglerElements.nextMovementCountdown) return;
      
      // Clear any existing countdown
      if (countdownInterval) {
        clearInterval(countdownInterval);
      }
      
      // Update countdown display immediately
      updateCountdownDisplay();
      
      // Setup continuous countdown updates every second
      countdownInterval = setInterval(updateCountdownDisplay, 1000);
    }
    
    // Update countdown display
    function updateCountdownDisplay() {
      if (!jigglerElements.nextMovementCountdown) return;
      
      // Check if jiggler is enabled
      if (!deviceInfo.jigglerEnabled) {
        jigglerElements.nextMovementCountdown.textContent = "Jiggler is disabled";
        return;
      }
      
      // Calculate time until next movement
      const now = new Date().getTime();
      const millisSinceLastCheck = now - (deviceInfo.lastCheckTime || now);
      const estimatedUptime = deviceInfo.uptime + millisSinceLastCheck;
      
      // Only proceed if we have valid next move time
      if (!deviceInfo.nextMoveTime) {
        jigglerElements.nextMovementCountdown.textContent = "Waiting for next move";
        return;
      }
      
      const timeUntilNext = Math.max(0, deviceInfo.nextMoveTime - estimatedUptime);
      
      if (timeUntilNext > 0) {
        const minutes = Math.floor(timeUntilNext / 60000);
        const seconds = Math.floor((timeUntilNext % 60000) / 1000);
        
        // Format as MM:SS
        const formattedTime = `${minutes}:${seconds.toString().padStart(2, '0')}`;
        jigglerElements.nextMovementCountdown.textContent = formattedTime;
      } else {
        jigglerElements.nextMovementCountdown.textContent = "Imminent";
      }
    }
    
    // Add entry to activity log
    function addToLog(message) {
      try {
        if (!jigglerElements.activityLog) return;
        
        const now = new Date();
        const timeString = now.toLocaleTimeString();
        const logEntry = document.createElement('div');
        logEntry.textContent = `[${timeString}] ${message}`;
        
        // Add to top of log
        jigglerElements.activityLog.insertBefore(logEntry, jigglerElements.activityLog.firstChild);
        
        // Limit log entries
        if (jigglerElements.activityLog.children.length > 50) {
          jigglerElements.activityLog.removeChild(jigglerElements.activityLog.lastChild);
        }
      } catch (error) {
        console.error("Error adding to log:", error);
      }
    }
    
    // Save configuration
    async function saveConfig() {
      try {
        if (!jigglerElements.jigglerEnabledCheckbox || !jigglerElements.movementPatternSelect || !jigglerElements.moveIntervalInput ||
            !jigglerElements.movementSizeInput || !jigglerElements.movementSpeedInput) {
          console.error("Missing form elements for save");
          return;
        }
        
        const jigglerEnabled = jigglerElements.jigglerEnabledCheckbox.checked;
        const movementPattern = jigglerElements.movementPatternSelect.value;
        const moveInterval = parseInt(jigglerElements.moveIntervalInput.value);
        const movementSize = parseInt(jigglerElements.movementSizeInput.value) * 2; // Multiply by 2
        const movementSpeed = parseInt(jigglerElements.movementSpeedInput.value);
        
        // New feature values
        const randomDelay = jigglerElements.randomDelayCheckbox ? jigglerElements.randomDelayCheckbox.checked : false;
        const movementTrail = jigglerElements.movementTrailCheckbox ? jigglerElements.movementTrailCheckbox.checked : false;
        
        // Validate input
        if (isNaN(moveInterval) || moveInterval < 1) {
          showStatus('Movement interval must be a positive number', false);
          return;
        }
        
        if (isNaN(movementSize) || movementSize < 1 || movementSize > 200) {
          showStatus('Movement size must be between 1 and 200', false);
          return;
        }
        
        if (isNaN(movementSpeed) || movementSpeed < 1 || movementSpeed > 3000) {
          showStatus('Movement speed must be between 1 and 3000 milliseconds', false);
          return;
        }
        
        const config = {
          jiggler_enabled: jigglerEnabled,
          movement_pattern: movementPattern,
          move_interval: moveInterval,
          movement_size: movementSize,
          movement_speed: movementSpeed,
          random_delay: randomDelay,
          movement_trail: movementTrail
        };
        
        // For backward compatibility with old versions
        config.circular_movement = (movementPattern === 'circular');
        
        // Set X and Y to the same value for backward compatibility
        config.movement_x = movementSize;
        config.movement_y = movementSize;
        
        const response = await fetch('/api/config', {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          credentials: 'same-origin',
          body: JSON.stringify(config)
        });
        
        if (!response.ok) {
          throw new Error('Failed to save configuration');
        }
        
        if (jigglerElements.saveStatus) {
          jigglerElements.saveStatus.innerHTML = '<i class="bi bi-check-circle-fill text-success"></i> Saved';
          
          setTimeout(() => {
            jigglerElements.saveStatus.innerHTML = '';
          }, 3000);
        }
        
        addToLog('Configuration saved');
        
        // Update the countdown display after saving config
        updateCountdownDisplay();
      } catch (error) {
        console.error("Save error:", error);
        if (jigglerElements.saveStatus) {
          jigglerElements.saveStatus.innerHTML = '<i class="bi bi-exclamation-triangle-fill text-danger"></i> Error saving';
        }
        addToLog('Error: ' + error.message);
      }
    }
    
    // Test mouse movement
    async function testMovement() {
      try {
        const response = await fetch('/api/move', {
          method: 'POST',
          credentials: 'same-origin'
        });
        
        if (!response.ok) {
          throw new Error('Failed to trigger movement');
        }
        
        showStatus('Mouse movement triggered', true);
        addToLog('Test movement triggered');
        
        // Refresh last movement time
        setTimeout(checkLastMovement, 1000);
        
        // Clear any existing countdown until we get fresh data
        if (jigglerElements.nextMovementCountdown) {
          jigglerElements.nextMovementCountdown.textContent = "Refreshing...";
        }
      } catch (error) {
        console.error("Test movement error:", error);
        showStatus(error.message, false);
      }
    }
    
    // Show status message
    function showStatus(message, isSuccess) {
      try {
        if (!jigglerElements.statusDiv) return;
        
        jigglerElements.statusDiv.textContent = message;
        jigglerElements.statusDiv.className = isSuccess ? 'alert alert-success mt-3' : 'alert alert-danger mt-3';
        jigglerElements.statusDiv.classList.remove('d-none');
        
        // Hide status after 3 seconds
        setTimeout(() => {
          jigglerElements.statusDiv.classList.add('d-none');
        }, 3000);
      } catch (error) {
        console.error("Status display error:", error);
      }
    }
    
    // Define logout function for Basic Auth
    function logout() {
      // For HTTP Basic Auth, just reload the page to trigger the auth dialog again
      window.location.reload();
    }
    
    // Add event listener for logout button
    if (logoutButton) {
      logoutButton.addEventListener('click', logout);
    }
    
    // Start the authentication check
    checkAuth();
  } catch (error) {
    console.error("Dashboard initialization error:", error);
  }
}); 