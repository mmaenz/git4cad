// API client for git4cad backend

export function getApiUrl(): string {
	return import.meta.env.VITE_API_URL || '';
}

// ---- Types ----

export interface RepoInfo {
	name: string;
	owner: string;
	description: string;
	clone_url: string;
	default_branch: string;
	empty: boolean;
	private: boolean;
	z_up: boolean;
	resolve_links: boolean;
	// Only meaningful when resolve_links is true — when true, the viewer
	// stops individually tagging parts past the first level of linked
	// children, grouping each child's whole sub-assembly into one
	// clickable/selectable unit.
	group_child_assemblies: boolean;
}

export interface MemberInfo {
	username: string;
	can_push: boolean;
}

export interface CommitInfo {
	sha: string;
	message: string;
	author: string;
	email: string;
	timestamp: number;  // Unix epoch seconds
}

export interface TreeEntry {
	name: string;
	type: 'blob' | 'tree';
	sha: string;
	size: number;
}

// ---- Auth token ----

const TOKEN_KEY = 'git4cad_token';
const USERNAME_KEY = 'git4cad_username';

export function setToken(token: string): void {
	localStorage.setItem(TOKEN_KEY, token);
}

export function getToken(): string | null {
	if (typeof localStorage === 'undefined') return null;
	return localStorage.getItem(TOKEN_KEY);
}

export function clearToken(): void {
	localStorage.removeItem(TOKEN_KEY);
	localStorage.removeItem(USERNAME_KEY);
}

export function setStoredUsername(username: string): void {
	localStorage.setItem(USERNAME_KEY, username);
}

export function getStoredUsername(): string | null {
	if (typeof localStorage === 'undefined') return null;
	return localStorage.getItem(USERNAME_KEY);
}

// ---- Core fetch ----

export async function apiFetch(path: string, options: RequestInit = {}): Promise<Response> {
	const token = getToken();
	const headers = new Headers(options.headers);

	if (token && !headers.has('Authorization')) {
		const username = getStoredUsername();
		const credential = username ? `${username}:${token}` : token;
		headers.set('Authorization', `Bearer ${credential}`);
	}

	if (!headers.has('Content-Type') && options.body && typeof options.body === 'string') {
		headers.set('Content-Type', 'application/json');
	}

	const url = `${getApiUrl()}${path}`;
	return fetch(url, { ...options, headers });
}

// ---- Auth ----

export async function login(username: string, password: string): Promise<{ token: string }> {
	const credentials = btoa(`${username}:${password}`);
	const res = await fetch(`${getApiUrl()}/api/v1/auth/login`, {
		method: 'POST',
		headers: {
			Authorization: `Basic ${credentials}`
		}
	});
	if (!res.ok) {
		const text = await res.text();
		throw new Error(text || `Login failed: ${res.status}`);
	}
	return res.json();
}

export async function register(
	username: string,
	password: string
): Promise<{ username: string; token: string }> {
	const res = await fetch(`${getApiUrl()}/api/v1/users`, {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		body: JSON.stringify({ username, password })
	});
	if (!res.ok) {
		const text = await res.text();
		throw new Error(text || `Registration failed: ${res.status}`);
	}
	return res.json();
}

// ---- Repos ----

export async function listRepos(): Promise<RepoInfo[]> {
	const res = await apiFetch('/api/v1/repos');
	if (!res.ok) throw new Error(`Failed to list repos: ${res.status}`);
	return res.json();
}

export async function createRepo(
	name: string,
	description?: string,
	isPrivate = false
): Promise<RepoInfo> {
	const res = await apiFetch('/api/v1/repos', {
		method: 'POST',
		body: JSON.stringify({ name, description, private: isPrivate })
	});
	if (!res.ok) {
		const text = await res.text();
		throw new Error(text || `Failed to create repo: ${res.status}`);
	}
	return res.json();
}

export async function updateRepo(
	user: string,
	repo: string,
	patch: { private?: boolean; z_up?: boolean; resolve_links?: boolean; group_child_assemblies?: boolean }
): Promise<RepoInfo> {
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}`,
		{ method: 'PATCH', body: JSON.stringify(patch) }
	);
	if (!res.ok) {
		const text = await res.text();
		throw new Error(text || `Failed to update repo: ${res.status}`);
	}
	return res.json();
}

export async function listMembers(user: string, repo: string): Promise<MemberInfo[]> {
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/members`
	);
	if (!res.ok) throw new Error(`Failed to list members: ${res.status}`);
	return res.json();
}

export async function addMember(
	user: string,
	repo: string,
	member: string,
	canPush: boolean
): Promise<MemberInfo> {
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/members/${encodeURIComponent(member)}`,
		{ method: 'PUT', body: JSON.stringify({ can_push: canPush }) }
	);
	if (!res.ok) {
		const text = await res.text();
		throw new Error(text || `Failed to add member: ${res.status}`);
	}
	return res.json();
}

export async function removeMember(user: string, repo: string, member: string): Promise<void> {
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/members/${encodeURIComponent(member)}`,
		{ method: 'DELETE' }
	);
	if (!res.ok) throw new Error(`Failed to remove member: ${res.status}`);
}

export async function getRepo(user: string, repo: string): Promise<RepoInfo> {
	const res = await apiFetch(`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}`);
	if (!res.ok) throw new Error(`Failed to get repo: ${res.status}`);
	return res.json();
}

export async function deleteRepo(user: string, repo: string): Promise<void> {
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}`,
		{ method: 'DELETE' }
	);
	if (!res.ok) throw new Error(`Failed to delete repo: ${res.status}`);
}

// ---- Commits ----

export async function listCommits(
	user: string,
	repo: string,
	ref?: string,
	limit = 30,
	offset = 0
): Promise<CommitInfo[]> {
	const params = new URLSearchParams();
	if (ref) params.set('ref', ref);
	params.set('limit', String(limit));
	params.set('offset', String(offset));
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/commits?${params}`
	);
	if (!res.ok) throw new Error(`Failed to list commits: ${res.status}`);
	return res.json();
}

export async function getCommit(user: string, repo: string, sha: string): Promise<CommitInfo> {
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/commits/${sha}`
	);
	if (!res.ok) throw new Error(`Failed to get commit: ${res.status}`);
	return res.json();
}

// ---- Tree ----

export async function getTree(
	user: string,
	repo: string,
	ref: string,
	path?: string
): Promise<TreeEntry[]> {
	const encodedRef = encodeURIComponent(ref);
	const url = path
		? `/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/tree/${encodedRef}/${path}`
		: `/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/tree/${encodedRef}`;
	const res = await apiFetch(url);
	if (!res.ok) throw new Error(`Failed to get tree: ${res.status}`);
	return res.json();
}

// ---- Blob / GLB URLs ----

export function getBlobUrl(user: string, repo: string, ref: string, path: string): string {
	return `${getApiUrl()}/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/blob/${encodeURIComponent(ref)}/${path}`;
}

export function getGlbUrl(
	user: string,
	repo: string,
	sha: string,
	path: string,
	light = false
): string {
	const suffix = light ? '?light=1' : '';
	return `${getApiUrl()}/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/glb/${sha}/${path}${suffix}`;
}

export async function getGlbStatus(
	user: string,
	repo: string,
	sha: string,
	path: string,
	light = false
): Promise<{ status: 'pending' | 'processing' | 'ready' | 'error' }> {
	const suffix = light ? '?light=1' : '';
	const res = await apiFetch(
		`/api/v1/repos/${encodeURIComponent(user)}/${encodeURIComponent(repo)}/glb/${sha}/${path}/status${suffix}`
	);
	if (!res.ok) throw new Error(`Failed to get GLB status: ${res.status}`);
	return res.json();
}

// ---- Utilities ----

export function relativeTime(dateStr: string): string {
	const date = new Date(dateStr);
	const now = new Date();
	const diffMs = now.getTime() - date.getTime();
	const diffSec = Math.floor(diffMs / 1000);

	if (diffSec < 60) return 'just now';
	const diffMin = Math.floor(diffSec / 60);
	if (diffMin < 60) return `${diffMin} minute${diffMin !== 1 ? 's' : ''} ago`;
	const diffHr = Math.floor(diffMin / 60);
	if (diffHr < 24) return `${diffHr} hour${diffHr !== 1 ? 's' : ''} ago`;
	const diffDay = Math.floor(diffHr / 24);
	if (diffDay < 30) return `${diffDay} day${diffDay !== 1 ? 's' : ''} ago`;
	const diffMon = Math.floor(diffDay / 30);
	if (diffMon < 12) return `${diffMon} month${diffMon !== 1 ? 's' : ''} ago`;
	const diffYr = Math.floor(diffMon / 12);
	return `${diffYr} year${diffYr !== 1 ? 's' : ''} ago`;
}

export function formatBytes(bytes: number): string {
	if (bytes === 0) return '0 B';
	if (bytes < 1024) return `${bytes} B`;
	if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
	return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export function is3DFile(path: string): boolean {
	return /\.(step|stp|fcstd)$/i.test(path);
}
