# DeLock
Decided to make my code public for a lockdown browser bypass window. The window is designed to circumvent lockdown browser's security measures with a hidden window containing an LLM wrapper with the ability to scan the users screen. The user is also able to ask questions directly into chat window. This was originally private and for educational purposes only, however my personal account used for testing was flagged by lockdown themselves after the window triggered a GetForegroundWindow() check. The program has now been updated to bypass this check but will likely be blacklisted in the future. 

# Note
This program requires an active OpenAI API key to function. It is currently set up to use 6.1 Luna as this model contains the best vision to performance to price ratio. You can configure the model at will within the Config file inside the main window folder.
