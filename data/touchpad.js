document.addEventListener('DOMContentLoaded', function() {
  try {
    // Get references to DOM elements
    const loadingDiv = document.getElementById('loading');
    const contentDiv = document.getElementById('content');
    const touchpad = document.getElementById('touchpad');
    const cursorIndicator = document.getElementById('cursor-indicator');
    const leftClick = document.getElementById('left-click');
    const rightClick = document.getElementById('right-click');
    const scrollUp = document.getElementById('scroll-up');
    const scrollDown = document.getElementById('scroll-down');
    const sensitivity = document.getElementById('sensitivity');
    const statusDiv = document.getElementById('status');
    const logoutButton = document.getElementById('logout-button');
    const touchpadEnabled = document.getElementById('touchpad-enabled');
    const touchpadContainer = document.getElementById('touchpad-container');
    
    // Touchpad state
    let isTracking = false;
    let lastX = 0;
    let lastY = 0;
    let lastMove = 0;
    const moveThrottleMs = 20;
    let touchStartX = 0;
    let touchStartY = 0;
    const touchThreshold = 3;
    
    // Scroll state
    let scrollInterval = null;
    const scrollRepeatDelay = 150;
    
    // Drag state
    let isDragging = false;
    let leftButtonPressed = false;
    
    // Handle touchpad toggle
    if (touchpadEnabled) {
      touchpadEnabled.addEventListener('change', function() {
        if (this.checked) {
          // Enable touchpad
          touchpadContainer.classList.remove('disabled-container');
          touchpadContainer.classList.add('enabled-container');
          showStatus('Touchpad enabled - Experimental feature', true);
        } else {
          // Disable touchpad
          touchpadContainer.classList.remove('enabled-container');
          touchpadContainer.classList.add('disabled-container');
          
          // Clean up any ongoing operations
          if (scrollInterval) {
            clearInterval(scrollInterval);
            scrollInterval = null;
          }
          
          if (isDragging) {
            isDragging = false;
            sendMouseButtonState('left', 'release');
          }
          
          isTracking = false;
          if (cursorIndicator) {
            cursorIndicator.style.display = 'none';
          }
          
          showStatus('Touchpad disabled', false);
        }
      });
    }
    
    // Check authentication
    async function checkAuth() {
      try {
        // Basic Auth is handled by the browser, just show content
        if (loadingDiv) loadingDiv.style.display = 'none';
        if (contentDiv) contentDiv.style.display = 'block';
      } catch (error) {
        console.error("Error initializing page:", error);
      }
    }
    
    // Get sensitivity value (1-10)
    function getSensitivity() {
      return sensitivity ? parseInt(sensitivity.value) : 5;
    }
    
    // Calculate mouse movement based on sensitivity
    function calculateMovement(dx, dy) {
      const sens = getSensitivity();
      const factor = sens * 0.8;
      return {
        x: Math.round(dx * factor),
        y: Math.round(dy * factor)
      };
    }
    
    // Send mouse movement to server
    async function sendMouseMove(x, y) {
      try {
        const now = Date.now();
        if (now - lastMove < moveThrottleMs) return;
        lastMove = now;
        
        await fetch('/api/touchpad/move', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          credentials: 'same-origin',
          body: JSON.stringify({ x, y })
        });
      } catch (error) {
        console.error("Error sending mouse movement:", error);
        showStatus('Connection error', false);
      }
    }
    
    // Send mouse click to server
    async function sendMouseClick(button, clickType = 'single') {
      try {
        await fetch('/api/touchpad/click', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          credentials: 'same-origin',
          body: JSON.stringify({ button, clickType })
        });
      } catch (error) {
        console.error("Error sending mouse click:", error);
        showStatus('Connection error', false);
      }
    }
    
    // Send mouse button state to server
    async function sendMouseButtonState(button, state) {
      try {
        await fetch('/api/touchpad/button', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          credentials: 'same-origin',
          body: JSON.stringify({ button, state })
        });
      } catch (error) {
        console.error("Error sending mouse button state:", error);
        showStatus('Connection error', false);
      }
    }
    
    // Send mouse scroll to server
    async function sendMouseScroll(amount) {
      try {
        const scrollMultiplier = 300;
        const scaledAmount = amount * scrollMultiplier;
        
        await fetch('/api/touchpad/scroll', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          credentials: 'same-origin',
          body: JSON.stringify({ amount: scaledAmount })
        });
      } catch (error) {
        console.error("Error sending mouse scroll:", error);
        showStatus('Connection error', false);
      }
    }
    
    // Handle touchpad touch/mouse events
    function setupTouchpad() {
      if (!touchpad) return;
      
      // Check if already initialized through a data attribute
      if (touchpad.dataset.initialized === 'true') return;
      
      // Mark as initialized
      touchpad.dataset.initialized = 'true';
      
      // Prevent default browser behaviors
      touchpad.addEventListener('dragstart', e => { e.preventDefault(); return false; });
      touchpad.addEventListener('selectstart', e => { e.preventDefault(); return false; });
      touchpad.addEventListener('contextmenu', e => { e.preventDefault(); return false; });
      
      // Mouse event handlers
      touchpad.addEventListener('mousedown', function(e) {
        if (!touchpadEnabled || !touchpadEnabled.checked) return;
        handleTouchpadMouseDown(e);
      });
      
      document.addEventListener('mousemove', function(e) {
        if (!touchpadEnabled || !touchpadEnabled.checked) return;
        moveTracking(e);
      });
      
      document.addEventListener('mouseup', function(e) {
        // Always handle mouseup to prevent stuck buttons
        stopTracking(e);
      });
      
      // Touch event handlers
      touchpad.addEventListener('touchstart', function(e) {
        if (!touchpadEnabled || !touchpadEnabled.checked) return;
        handleTouchpadTouchStart(e);
      }, { passive: false });
      
      document.addEventListener('touchmove', function(e) {
        if (!touchpadEnabled || !touchpadEnabled.checked) return;
        moveTracking(e);
      }, { passive: false });
      
      document.addEventListener('touchend', function(e) {
        // Always handle touchend to prevent stuck states
        stopTracking(e);
      }, { passive: false });
      
      // Button event handlers
      if (leftClick) {
        leftClick.addEventListener('mousedown', function(e) {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          e.preventDefault();
          e.stopPropagation();
          leftButtonPressed = true;
          sendMouseButtonState('left', 'press');
          leftClick.classList.add('active');
        });
        
        leftClick.addEventListener('touchstart', function(e) {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          e.preventDefault();
          e.stopPropagation();
          leftButtonPressed = true;
          sendMouseButtonState('left', 'press');
          leftClick.classList.add('active');
        });
        
        // When released outside the button
        document.addEventListener('mouseup', function(e) {
          if (leftButtonPressed) {
            leftButtonPressed = false;
            sendMouseButtonState('left', 'release');
            leftClick.classList.remove('active');
          }
        });
        
        document.addEventListener('touchend', function(e) {
          if (leftButtonPressed) {
            leftButtonPressed = false;
            sendMouseButtonState('left', 'release');
            leftClick.classList.remove('active');
          }
        });
      }
      
      if (rightClick) {
        rightClick.addEventListener('click', function(e) {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          e.preventDefault();
          e.stopPropagation();
          sendMouseClick('right');
        });
      }
      
      // Scroll event handlers
      if (scrollUp) {
        scrollUp.addEventListener('mousedown', function() {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          sendMouseScroll(-10);
          scrollUp.classList.add('active');
          
          if (scrollInterval) clearInterval(scrollInterval);
          scrollInterval = setInterval(() => {
            sendMouseScroll(-10);
          }, 100);
        });
        
        scrollUp.addEventListener('touchstart', function(e) {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          e.preventDefault();
          sendMouseScroll(-10);
          scrollUp.classList.add('active');
          
          if (scrollInterval) clearInterval(scrollInterval);
          scrollInterval = setInterval(() => {
            sendMouseScroll(-10);
          }, 100);
        });
      }
      
      if (scrollDown) {
        scrollDown.addEventListener('mousedown', function() {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          sendMouseScroll(10);
          scrollDown.classList.add('active');
          
          if (scrollInterval) clearInterval(scrollInterval);
          scrollInterval = setInterval(() => {
            sendMouseScroll(10);
          }, 100);
        });
        
        scrollDown.addEventListener('touchstart', function(e) {
          if (!touchpadEnabled || !touchpadEnabled.checked) return;
          e.preventDefault();
          sendMouseScroll(10);
          scrollDown.classList.add('active');
          
          if (scrollInterval) clearInterval(scrollInterval);
          scrollInterval = setInterval(() => {
            sendMouseScroll(10);
          }, 100);
        });
      }
      
      // Add event listeners to stop continuous scrolling
      document.addEventListener('mouseup', stopContinuousScroll);
      document.addEventListener('touchend', stopContinuousScroll);
    }
    
    // Handle mouse down on the touchpad
    function handleTouchpadMouseDown(e) {
      e.preventDefault();
      e.stopPropagation();
      
      // Get position for tracking
      const pos = getEventPosition(e);
      touchStartX = pos.x;
      touchStartY = pos.y;
      
      // Start tracking movement
      startTracking(e);
    }
    
    // Handle touch start on the touchpad
    function handleTouchpadTouchStart(e) {
      e.preventDefault();
      e.stopPropagation();
      
      // Get position for tracking
      const pos = getEventPosition(e);
      touchStartX = pos.x;
      touchStartY = pos.y;
      
      // Start tracking movement
      startTracking(e);
    }
    
    // Start tracking mouse/touch movement
    function startTracking(e) {
      e.preventDefault();
      e.stopPropagation();
      isTracking = true;
      
      if (cursorIndicator) {
        cursorIndicator.style.display = 'block';
      }
      
      // Get initial position
      const pos = getEventPosition(e);
      lastX = pos.x;
      lastY = pos.y;
      
      // Update cursor indicator position
      updateCursorIndicator(pos.x, pos.y);
    }
    
    // Track mouse/touch movement
    function moveTracking(e) {
      if (!isTracking && !isDragging && !leftButtonPressed) return;
      e.preventDefault();
      e.stopPropagation();
      
      const pos = getEventPosition(e);
      
      // Calculate delta movement since last position
      const dx = pos.x - lastX;
      const dy = pos.y - lastY;
      
      // Only send move if there was actual movement above threshold
      if (Math.abs(dx) > touchThreshold || Math.abs(dy) > touchThreshold) {
        // Calculate movement based on sensitivity
        const movement = calculateMovement(dx, dy);
        
        // Send movement to server
        sendMouseMove(movement.x, movement.y);
        
        // Update last position
        lastX = pos.x;
        lastY = pos.y;
        
        // Update cursor indicator position
        updateCursorIndicator(pos.x, pos.y);
      }
    }
    
    // Stop tracking mouse/touch movement
    function stopTracking(e) {
      if (!isTracking && !isDragging) return;
      
      if (e) e.preventDefault();
      
      if (isDragging) {
        // End dragging operation
        isDragging = false;
        sendMouseButtonState('left', 'release');
      }
      
      isTracking = false;
      
      if (cursorIndicator) {
        cursorIndicator.style.display = 'none';
      }
    }
    
    // Get event position (handles both mouse and touch events)
    function getEventPosition(e) {
      let x, y;
      
      if (e.type.startsWith('touch')) {
        // Touch event
        const touch = e.touches[0] || e.changedTouches[0];
        const rect = touchpad.getBoundingClientRect();
        x = touch.clientX - rect.left;
        y = touch.clientY - rect.top;
        
        // Prevent edge cases where touch coordinates might be outside the touchpad
        x = Math.max(0, Math.min(x, rect.width));
        y = Math.max(0, Math.min(y, rect.height));
      } else {
        // Mouse event
        const rect = touchpad.getBoundingClientRect();
        x = e.clientX - rect.left;
        y = e.clientY - rect.top;
      }
      
      return { x, y };
    }
    
    // Update the cursor indicator position
    function updateCursorIndicator(x, y) {
      if (!cursorIndicator) return;
      
      // Keep indicator within touchpad bounds
      const pad = touchpad.getBoundingClientRect();
      x = Math.max(0, Math.min(x, pad.width));
      y = Math.max(0, Math.min(y, pad.height));
      
      // Change color based on state - blue for dragging
      if (isDragging || leftButtonPressed) {
        cursorIndicator.style.backgroundColor = 'rgba(13, 110, 253, 0.7)'; // Bootstrap primary blue
      } else {
        cursorIndicator.style.backgroundColor = 'rgba(255, 255, 255, 0.7)'; // Normal white
      }
      
      cursorIndicator.style.left = x + 'px';
      cursorIndicator.style.top = y + 'px';
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
    
    // Logout
    if (logoutButton) {
      logoutButton.addEventListener('click', function() {
        // For HTTP Basic Auth, just reload the page
        window.location.reload();
      });
    }
    
    // Define the logout function if it's needed
    function logout() {
      // For HTTP Basic Auth, just reload the page
      window.location.reload();
    }
    
    // Stop continuous scrolling
    function stopContinuousScroll() {
      if (scrollInterval) {
        clearInterval(scrollInterval);
        scrollInterval = null;
        
        // Remove active class from scroll buttons
        if (scrollUp) scrollUp.classList.remove('active');
        if (scrollDown) scrollDown.classList.remove('active');
      }
    }
    
    // Initialize
    checkAuth();
    setupTouchpad();
    
  } catch (error) {
    console.error("Initialization error:", error);
  }
}); 