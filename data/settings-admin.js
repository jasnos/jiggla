document.addEventListener('DOMContentLoaded', function() {
  // Get DOM elements
  const loadingDiv = document.getElementById('loading');
  const contentDiv = document.getElementById('content');
  const saveButton = document.getElementById('save-button');
  const rebootButton = document.getElementById('reboot-button');
  const statusDiv = document.getElementById('status');
  const settingsForm = document.getElementById('settings-form');
  
  // Form inputs
  const authUsername = document.getElementById('auth-username');
  const authPassword = document.getElementById('auth-password');
  const authPasswordConfirm = document.getElementById('auth-password-confirm');
  const apSSID = document.getElementById('ap-ssid');
  const apPassword = document.getElementById('ap-password');
  const hostname = document.getElementById('hostname');
  const hiddenAP = document.getElementById('hidden-ap');
  const webPort = document.getElementById('web-port');
  
  // WiFi mode settings
  const wifiModeAP = document.getElementById('wifi-mode-ap');
  const wifiModeAPSTA = document.getElementById('wifi-mode-apsta');
  const staSettingsContainer = document.getElementById('sta-settings-container');
  const staSSID = document.getElementById('sta-ssid');
  const staPassword = document.getElementById('sta-password');
  const apAvailabilityAlways = document.getElementById('ap-availability-always');
  const apAvailabilityTimeout = document.getElementById('ap-availability-timeout');
  const apTimeout = document.getElementById('ap-timeout');
  const apTimeoutContainer = document.getElementById('ap-timeout-container');
  
  // Show/hide STA settings based on WiFi mode selection
  wifiModeAPSTA.addEventListener('change', function() {
    if (this.checked) {
      staSettingsContainer.style.display = 'block';
    }
  });
  
  wifiModeAP.addEventListener('change', function() {
    if (this.checked) {
      staSettingsContainer.style.display = 'none';
    }
  });
  
  // Show/hide AP timeout input based on selection
  apAvailabilityTimeout.addEventListener('change', function() {
    if (this.checked) {
      apTimeoutContainer.style.display = 'block';
    }
  });
  
  apAvailabilityAlways.addEventListener('change', function() {
    if (this.checked) {
      apTimeoutContainer.style.display = 'none';
    }
  });
  
  // Check authentication and load settings
  async function init() {
    try {
      // Check if user is authenticated
      const authResponse = await fetch('/api/auth/check', {
        credentials: 'same-origin',
        cache: 'no-store'
      });
      
      if (!authResponse.ok) {
        // Not authenticated, redirect to login
        window.location.href = '/login';
        return;
      }
      
      // Load settings
      await loadSettings();
      
      // Show content
      loadingDiv.style.display = 'none';
      contentDiv.style.display = 'block';
    } catch (error) {
      console.error('Initialization error:', error);
      showStatus('Error initializing page: ' + error.message, false);
    }
  }
  
  // Load settings from server
  async function loadSettings() {
    try {
      const response = await fetch('/api/settings', {
        credentials: 'same-origin'
      });
      
      if (!response.ok) {
        throw new Error('Failed to load settings');
      }
      
      const settings = await response.json();
      
      // Populate form fields
      // Authentication
      if (settings.auth) {
        authUsername.value = settings.auth.username || '';
      }
      
      // AP settings
      if (settings.ap) {
        apSSID.value = settings.ap.ssid || '';
        apPassword.value = settings.ap.password || '';
        hiddenAP.checked = settings.ap.hidden || false;
      }
      
      // Hostname
      hostname.value = settings.hostname || '';
      
      // WiFi mode settings
      if (settings.wifi_mode) {
        if (settings.wifi_mode === 'apsta') {
          wifiModeAPSTA.checked = true;
          staSettingsContainer.style.display = 'block';
        } else {
          wifiModeAP.checked = true;
          staSettingsContainer.style.display = 'none';
        }
        
        if (settings.ap_availability === 'timeout') {
          apAvailabilityTimeout.checked = true;
          apTimeoutContainer.style.display = 'block';
          apTimeout.value = settings.ap_timeout || 5;
        } else {
          apAvailabilityAlways.checked = true;
          apTimeoutContainer.style.display = 'none';
        }
      }
      
      // STA settings
      if (settings.sta) {
        staSSID.value = settings.sta.ssid || '';
        staPassword.value = settings.sta.password || '';
      }
      
      // Web port
      webPort.value = settings.web_port || 8787;
      
    } catch (error) {
      console.error('Settings load error:', error);
      showStatus('Failed to load settings: ' + error.message, false);
    }
  }
  
  // Save settings to server
  async function saveSettings() {
    try {
      // Validate form
      if (!validateForm()) {
        return;
      }
      
      // Prepare settings object
      const settings = {
        auth: {
          username: authUsername.value,
          password: authPassword.value
        },
        ap: {
          ssid: apSSID.value,
          password: apPassword.value,
          hidden: hiddenAP.checked
        },
        hostname: hostname.value,
        sta: {
          ssid: staSSID.value,
          password: staPassword.value
        },
        wifi_mode: wifiModeAPSTA.checked ? 'apsta' : 'ap',
        ap_availability: apAvailabilityTimeout.checked ? 'timeout' : 'always',
        ap_timeout: parseInt(apTimeout.value),
        web_port: parseInt(webPort.value)
      };
      
      // Send to server
      const response = await fetch('/api/settings', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        credentials: 'same-origin',
        body: JSON.stringify(settings)
      });
      
      if (!response.ok) {
        const data = await response.json();
        throw new Error(data.message || 'Failed to save settings');
      }
      
      showStatus('Settings saved successfully! A device reboot may be required for some changes to take effect.', true);
    } catch (error) {
      console.error('Settings save error:', error);
      showStatus('Failed to save settings: ' + error.message, false);
    }
  }
  
  // Validate form inputs
  function validateForm() {
    // Check username
    if (!authUsername.value.trim()) {
      showStatus('Username cannot be empty', false);
      authUsername.focus();
      return false;
    }
    
    // Check password
    if (authPassword.value && authPassword.value.length < 8) {
      showStatus('Password must be at least 8 characters long', false);
      authPassword.focus();
      return false;
    }
    
    // Check password confirmation
    if (authPassword.value !== authPasswordConfirm.value) {
      showStatus('Passwords do not match', false);
      authPasswordConfirm.focus();
      return false;
    }
    
    // Check AP SSID
    if (!apSSID.value.trim()) {
      showStatus('Access Point SSID cannot be empty', false);
      apSSID.focus();
      return false;
    }
    
    // Check AP password
    if (apPassword.value.length < 8) {
      showStatus('Access Point password must be at least 8 characters long', false);
      apPassword.focus();
      return false;
    }
    
    // Check hostname
    if (!hostname.value.trim()) {
      showStatus('Hostname cannot be empty', false);
      hostname.focus();
      return false;
    }
    
    // Check web port
    const port = parseInt(webPort.value);
    if (isNaN(port) || port < 1 || port > 65535) {
      showStatus('Web port must be between 1 and 65535', false);
      webPort.focus();
      return false;
    }
    
    // Check STA credentials if AP+STA mode is selected
    if (wifiModeAPSTA.checked) {
      if (!staSSID.value.trim()) {
        showStatus('WiFi Network SSID cannot be empty in AP+STA mode', false);
        staSSID.focus();
        return false;
      }
      
      if (!staPassword.value.trim() || staPassword.value.length < 8) {
        showStatus('WiFi Network password must be at least 8 characters long in AP+STA mode', false);
        staPassword.focus();
        return false;
      }
    }
    
    return true;
  }
  
  // Reboot device
  async function rebootDevice() {
    try {
      if (!confirm('Are you sure you want to reboot the device?')) {
        return;
      }
      
      saveButton.disabled = true;
      rebootButton.disabled = true;
      
      showStatus('Rebooting device...', true);
      
      const response = await fetch('/api/reboot', {
        method: 'POST',
        credentials: 'same-origin'
      });
      
      if (!response.ok) {
        throw new Error('Failed to reboot device');
      }
      
      showStatus('Device is rebooting. Please wait a moment before reconnecting.', true);
      
      // Redirect to login page after a delay
      setTimeout(() => {
        window.location.href = '/login';
      }, 5000);
    } catch (error) {
      console.error('Reboot error:', error);
      showStatus('Failed to reboot device: ' + error.message, false);
      saveButton.disabled = false;
      rebootButton.disabled = false;
    }
  }
  
  // Show status message
  function showStatus(message, isSuccess) {
    statusDiv.textContent = message;
    statusDiv.className = isSuccess ? 'alert alert-success mt-3' : 'alert alert-danger mt-3';
    statusDiv.classList.remove('d-none');
    
    // Scroll to status message
    statusDiv.scrollIntoView({ behavior: 'smooth' });
  }
  
  // Event listeners
  saveButton.addEventListener('click', saveSettings);
  rebootButton.addEventListener('click', rebootDevice);
  
  // Initialize page
  init();
}); 