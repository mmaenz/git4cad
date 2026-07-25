import { writable } from 'svelte/store';
import { browser } from '$app/environment';

type Theme = 'dark' | 'light';

function applyTheme(theme: Theme): void {
    document.documentElement.setAttribute('data-theme', theme);
}

function createThemeStore() {
    const { subscribe, set, update } = writable<Theme>('dark');

    return {
        subscribe,
        init(): void {
            if (!browser) { return; }
            const stored = (localStorage.getItem('theme') as Theme) ?? 'dark';
            set(stored);
            applyTheme(stored);
        },
        toggle(): void {
            update(current => {
                const next: Theme = current === 'dark' ? 'light' : 'dark';
                if (browser) {
                    localStorage.setItem('theme', next);
                    applyTheme(next);
                }
                return next;
            });
        }
    };
}

export const themeStore = createThemeStore();
