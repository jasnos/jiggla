document.addEventListener('DOMContentLoaded', function() {
  try {
    // Get references to DOM elements
    const loadingDiv = document.getElementById('loading');
    const contentDiv = document.getElementById('content');
    const jigglerEnabledCheckbox = document.getElementById('jiggler-enabled');
    const circularMovementCheckbox = document.getElementById('circular-movement');
    const moveIntervalInput = document.getElementById('move-interval');
    const movementXInput = document.getElementById('movement-x');
    const movementYInput = document.getElementById('movement-y');
    const movementSpeedInput = document.getElementById('movement-speed');
    const testButton = document.getElementById('test-button');
    const logoutButton = document.getElementById('logout-button');
    const statusDiv = document.getElementById('status');
    const saveStatus = document.getElementById('save-status');
    const xValue = document.getElementById('x-value');
    const yValue = document.getElementById('y-value');
    const lastMovement = document.getElementById('last-movement');
    const activityLog = document.getElementById('activity-log');
    const nextMovementCountdown = document.getElementById('next-movement-countdown');
    
    // Store device info for countdown
    let deviceInfo = {
      lastMoveTime: 0,
      nextMoveTime: 0,
      uptime: 0,
      jigglerEnabled: true
    };
    
    // Countdown timer interval
    let countdownInterval = null;
    
    // Verify all required elements exist
    if (!jigglerEnabledCheckbox || !moveIntervalInput || !movementXInput || 
        !movementYInput || !movementSpeedInput || !saveStatus) {
      console.error("Required form elements not found");
      return;
    }
    
    // Additional controls (new features)
    const randomDelayCheckbox = document.getElementById('random-delay');
    const movementTrailCheckbox = document.getElementById('movement-trail');
    
    // Auto-save timer
    let saveTimer = null;
    
    // Update slider values - add error handling
    if (movementXInput && xValue) {
      movementXInput.addEventListener('input', () => {
        xValue.textContent = movementXInput.value;
      });
    }
    
    if (movementYInput && yValue) {
      movementYInput.addEventListener('input', () => {
        yValue.textContent = movementYInput.value;
      });
    }
    
    // Auto-save function
    function triggerAutoSave() {
      if (!saveStatus) return;
      
      clearTimeout(saveTimer);
      saveStatus.innerHTML = '<i class="bi bi-hourglass-split text-warning"></i> Saving...';
      
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
    if (jigglerEnabledCheckbox) {
      jigglerEnabledCheckbox.addEventListener('change', function() {
        deviceInfo.jigglerEnabled = this.checked;
        updateCountdownDisplay();
      });
    }
    
    // Check authentication
    async function checkAuth() {
      try {
        const response = await fetch('/api/auth/check', {
          // Include credentials to send cookies properly
          credentials: 'same-origin',
          cache: 'no-store'
        });
        
        if (response.ok) {
          // Authenticated, show content
          if (loadingDiv) loadingDiv.style.display = 'none';
          if (contentDiv) contentDiv.style.display = 'block';
          loadConfig();
          
          // Setup a periodic check for last movement
          setInterval(checkLastMovement, 5000);
          
          // Add success message to log
          addToLog('Successfully authenticated');
        } else {
          console.log('Authentication failed, redirecting to login');
          // Not authenticated, redirect to login
          window.location.href = '/login';
        }
      } catch (error) {
        console.error("Authentication error:", error);
        // Error, redirect to login
        window.location.href = '/login?error=' + encodeURIComponent('Connection error');
      }
    }
    
    // Load current configuration
    async function loadConfig() {
      try {
        const response = await fetch('/api/config', {
          credentials: 'same-origin'
        });
        
        if (!response.ok) {
          throw new Error('Failed to load configuration');
        }
        
        const config = await response.json();
        
        if (jigglerEnabledCheckbox) {
          jigglerEnabledCheckbox.checked = !!config.jiggler_enabled;
          deviceInfo.jigglerEnabled = !!config.jiggler_enabled;
        }
        if (circularMovementCheckbox) circularMovementCheckbox.checked = !!config.circular_movement;
        if (moveIntervalInput) moveIntervalInput.value = config.move_interval || 540;
        if (movementXInput) movementXInput.value = config.movement_x || 5;
        if (movementYInput) movementYInput.value = config.movement_y || 5;
        if (movementSpeedInput) movementSpeedInput.value = config.movement_speed || 10;
        
        // Set values for ranged inputs
        if (xValue) xValue.textContent = config.movement_x || 5;
        if (yValue) yValue.textContent = config.movement_y || 5;
        
        // New features (with defaults if not in config yet)
        if (randomDelayCheckbox) randomDelayCheckbox.checked = !!config.random_delay;
        if (movementTrailCheckbox) movementTrailCheckbox.checked = !!config.movement_trail;
        
        // Update last movement time
        checkLastMovement();
        
        addToLog('Configuration loaded');
      } catch (error) {
        console.error("Config load error:", error);
        showStatus('Failed to load configuration: ' + error.message, false);
      }
    }
    
    // Get last movement time
    async function checkLastMovement() {
      try {
        if (!lastMovement) return;
        
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
              lastMovement.textContent = `${Math.floor(timeSinceLastMove / 1000)} seconds ago`;
            } else if (timeSinceLastMove < 3600000) {
              // Less than an hour
              lastMovement.textContent = `${Math.floor(timeSinceLastMove / 60000)} minutes ago`;
            } else {
              // Hours and minutes
              const hours = Math.floor(timeSinceLastMove / 3600000);
              const minutes = Math.floor((timeSinceLastMove % 3600000) / 60000);
              lastMovement.textContent = `${hours}h ${minutes}m ago`;
            }
            
            // Start or update countdown timer
            updateCountdownTimer();
          } else {
            lastMovement.textContent = "No movements yet";
            if (nextMovementCountdown) {
              nextMovementCountdown.textContent = deviceInfo.jigglerEnabled ? "Waiting for first movement" : "Jiggler is disabled";
            }
          }
        }
      } catch (error) {
        console.error("Status check error:", error);
      }
    }
    
    // Update countdown timer
    function updateCountdownTimer() {
      if (!nextMovementCountdown) return;
      
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
      if (!nextMovementCountdown) return;
      
      // Check if jiggler is enabled
      if (!deviceInfo.jigglerEnabled) {
        nextMovementCountdown.textContent = "Jiggler is disabled";
        return;
      }
      
      // Calculate time until next movement
      const now = new Date().getTime();
      const millisSinceLastCheck = now - (deviceInfo.lastCheckTime || now);
      const estimatedUptime = deviceInfo.uptime + millisSinceLastCheck;
      
      // Only proceed if we have valid next move time
      if (!deviceInfo.nextMoveTime) {
        nextMovementCountdown.textContent = "Waiting for next move";
        return;
      }
      
      const timeUntilNext = Math.max(0, deviceInfo.nextMoveTime - estimatedUptime);
      
      if (timeUntilNext > 0) {
        const minutes = Math.floor(timeUntilNext / 60000);
        const seconds = Math.floor((timeUntilNext % 60000) / 1000);
        
        // Format as MM:SS
        const formattedTime = `${minutes}:${seconds.toString().padStart(2, '0')}`;
        nextMovementCountdown.textContent = formattedTime;
      } else {
        nextMovementCountdown.textContent = "Imminent";
      }
    }
    
    // Add entry to activity log
    function addToLog(message) {
      try {
        if (!activityLog) return;
        
        const now = new Date();
        const timeString = now.toLocaleTimeString();
        const logEntry = document.createElement('div');
        logEntry.textContent = `[${timeString}] ${message}`;
        
        // Add to top of log
        activityLog.insertBefore(logEntry, activityLog.firstChild);
        
        // Limit log entries
        if (activityLog.children.length > 50) {
          activityLog.removeChild(activityLog.lastChild);
        }
      } catch (error) {
        console.error("Error adding to log:", error);
      }
    }
    
    // Save configuration
    async function saveConfig() {
      try {
        if (!jigglerEnabledCheckbox || !circularMovementCheckbox || !moveIntervalInput ||
            !movementXInput || !movementYInput || !movementSpeedInput) {
          console.error("Missing form elements for save");
          return;
        }
        
        const jigglerEnabled = jigglerEnabledCheckbox.checked;
        const circularMovement = circularMovementCheckbox.checked;
        const moveInterval = parseInt(moveIntervalInput.value);
        const movementX = parseInt(movementXInput.value);
        const movementY = parseInt(movementYInput.value);
        const movementSpeed = parseInt(movementSpeedInput.value);
        
        // New feature values
        const randomDelay = randomDelayCheckbox ? randomDelayCheckbox.checked : false;
        const movementTrail = movementTrailCheckbox ? movementTrailCheckbox.checked : false;
        
        // Validate input
        if (isNaN(moveInterval) || moveInterval < 1) {
          showStatus('Movement interval must be a positive number', false);
          return;
        }
        
        if (isNaN(movementX) || movementX < -200 || movementX > 200) {
          showStatus('Movement X must be between -200 and 200', false);
          return;
        }
        
        if (isNaN(movementY) || movementY < -200 || movementY > 200) {
          showStatus('Movement Y must be between -200 and 200', false);
          return;
        }
        
        if (isNaN(movementSpeed) || movementSpeed < 1 || movementSpeed > 1000) {
          showStatus('Movement speed must be between 1 and 1000 milliseconds', false);
          return;
        }
        
        const config = {
          jiggler_enabled: jigglerEnabled,
          circular_movement: circularMovement,
          move_interval: moveInterval,
          movement_x: movementX,
          movement_y: movementY,
          movement_speed: movementSpeed,
          random_delay: randomDelay,
          movement_trail: movementTrail
        };
        
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
        
        if (saveStatus) {
          saveStatus.innerHTML = '<i class="bi bi-check-circle-fill text-success"></i> Saved';
          
          setTimeout(() => {
            saveStatus.innerHTML = '';
          }, 3000);
        }
        
        addToLog('Configuration saved');
        
        // Update the countdown display after saving config
        updateCountdownDisplay();
      } catch (error) {
        console.error("Save error:", error);
        if (saveStatus) {
          saveStatus.innerHTML = '<i class="bi bi-exclamation-triangle-fill text-danger"></i> Error saving';
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
        if (nextMovementCountdown) {
          nextMovementCountdown.textContent = "Refreshing...";
        }
      } catch (error) {
        console.error("Test movement error:", error);
        showStatus(error.message, false);
      }
    }
    
    // Logout
    async function logout() {
      try {
        const response = await fetch('/api/auth/logout', {
          method: 'POST',
          credentials: 'same-origin'
        });
        
        if (response.ok) {
          window.location.href = '/login';
        } else {
          showStatus('Logout failed', false);
        }
      } catch (error) {
        console.error("Logout error:", error);
        showStatus('Logout failed: ' + error.message, false);
      }
    }
    
    // Show status message
    function showStatus(message, isSuccess) {
      try {
        if (!statusDiv) return;
        
        statusDiv.textContent = message;
        statusDiv.className = isSuccess ? 'alert alert-success mt-3' : 'alert alert-danger mt-3';
        statusDiv.classList.remove('d-none');
        
        // Hide status after 3 seconds
        setTimeout(() => {
          statusDiv.classList.add('d-none');
        }, 3000);
      } catch (error) {
        console.error("Status display error:", error);
      }
    }
    
    // Event listeners
    if (testButton) testButton.addEventListener('click', testMovement);
    if (logoutButton) logoutButton.addEventListener('click', logout);
    
    // Check authentication on page load
    checkAuth();
  } catch (error) {
    console.error("Initialization error:", error);
  }
}); 