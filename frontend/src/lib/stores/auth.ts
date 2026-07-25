import { writable } from 'svelte/store';
import {
	login as apiLogin,
	setToken,
	clearToken,
	getToken,
	getStoredUsername,
	setStoredUsername
} from '$lib/api';

export interface AuthState {
	username: string | null;
	token: string | null;
	loggedIn: boolean;
}

function createAuthStore() {
	// Initialize from localStorage (safe for SSR: getToken/getStoredUsername guard against missing localStorage)
	const initialToken = getToken();
	const initialUsername = getStoredUsername();

	const { subscribe, set, update } = writable<AuthState>({
		username: initialUsername,
		token: initialToken,
		loggedIn: initialToken !== null
	});

	return {
		subscribe,

		async login(username: string, password: string): Promise<void> {
			const result = await apiLogin(username, password);
			setToken(result.token);
			setStoredUsername(username);
			set({ username, token: result.token, loggedIn: true });
		},

		/** Set auth state directly from a token already obtained (e.g. from registration). */
		setFromToken(username: string, token: string): void {
			setToken(token);
			setStoredUsername(username);
			set({ username, token, loggedIn: true });
		},

		logout(): void {
			clearToken();
			set({ username: null, token: null, loggedIn: false });
		},

		/** Re-hydrate from localStorage (call on client mount if needed) */
		hydrate(): void {
			const token = getToken();
			const username = getStoredUsername();
			set({ username, token, loggedIn: token !== null });
		}
	};
}

export const authStore = createAuthStore();
