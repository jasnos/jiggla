document.addEventListener('DOMContentLoaded', function() {
  const usernameInput = document.getElementById('username');
  const passwordInput = document.getElementById('password');
  const loginButton = document.getElementById('login-button');
  const statusDiv = document.getElementById('status');
  
  // Check if already authenticated
  async function checkAuth() {
    try {
      console.log("Checking authentication status...");
      const response = await fetch('/api/auth/check', {
        credentials: 'same-origin',
        cache: 'no-store',
        headers: {
          'Pragma': 'no-cache',
          'Cache-Control': 'no-cache'
        }
      });
      
      console.log("Auth check response status:", response.status);
      
      if (response.ok) {
        // Already authenticated, redirect to main page
        console.log("Already authenticated, redirecting to main page");
        window.location.href = '/';
      } else {
        console.log("Not authenticated, staying on login page");
      }
    } catch (error) {
      // Not authenticated, stay on login page
      console.error("Auth check error:", error);
    }
  }
  
  // Handle login
  async function login() {
    const username = usernameInput.value;
    const password = passwordInput.value;
    
    if (!username || !password) {
      showStatus('Please enter both username and password', false);
      return;
    }
    
    loginButton.disabled = true;
    loginButton.innerHTML = '<span class="spinner-border spinner-border-sm" role="status" aria-hidden="true"></span> Logging in...';
    
    try {
      const response = await fetch('/api/auth/login', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        credentials: 'same-origin',
        cache: 'no-store',
        body: JSON.stringify({ username, password })
      });
      
      const data = await response.json();
      
      if (response.ok) {
        // Login successful, redirect to main page after a short delay
        showStatus('Login successful! Redirecting...', true);
        setTimeout(() => {
          window.location.href = '/';
        }, 1500);
      } else {
        showStatus(data.message || 'Login failed', false);
        loginButton.disabled = false;
        loginButton.innerHTML = 'Login';
      }
    } catch (error) {
      showStatus('Login failed: ' + error.message, false);
      loginButton.disabled = false;
      loginButton.innerHTML = 'Login';
    }
  }
  
  // Show status message
  function showStatus(message, isSuccess) {
    statusDiv.textContent = message;
    statusDiv.className = isSuccess ? 'alert alert-success mt-3' : 'alert alert-danger mt-3';
    statusDiv.classList.remove('d-none');
    
    // Hide status after 3 seconds for success messages
    if (isSuccess) {
      setTimeout(() => {
        statusDiv.classList.add('d-none');
      }, 3000);
    }
  }
  
  // Check for error parameter in URL
  function checkForErrors() {
    const urlParams = new URLSearchParams(window.location.search);
    const error = urlParams.get('error');
    if (error) {
      showStatus('Error: ' + error, false);
    }
  }
  
  // Event listeners
  loginButton.addEventListener('click', login);
  
  // Allow login with Enter key
  passwordInput.addEventListener('keypress', function(e) {
    if (e.key === 'Enter') {
      login();
    }
  });
  
  // Check if already authenticated
  checkAuth();
  
  // Check for error parameters
  checkForErrors();
}); 