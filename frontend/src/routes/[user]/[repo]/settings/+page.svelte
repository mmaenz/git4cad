<script lang="ts">
	import { onMount } from 'svelte';
	import { goto } from '$app/navigation';
	import { authStore } from '$lib/stores/auth';
	import {
		getRepo, updateRepo, listMembers, addMember, removeMember, deleteRepo,
		type RepoInfo, type MemberInfo
	} from '$lib/api';
	import type { PageProps } from './$types';

	let { params }: PageProps = $props();
	let user = $derived(params.user);
	let repo = $derived(params.repo);
	let isOwner = $derived($authStore.username === user);

	let repoInfo = $state<RepoInfo | null>(null);
	let members  = $state<MemberInfo[]>([]);
	let loading  = $state(true);
	let error    = $state('');

	// Visibility
	let togglingPrivacy = $state(false);

	// 3D viewer orientation
	let togglingZUp = $state(false);

	// 3D viewer linked assembly
	let togglingResolveLinks = $state(false);

	// Collaborators
	let addMemberName    = $state('');
	let addMemberCanPush = $state(false);
	let addMemberError   = $state('');
	let addingMember     = $state(false);

	// Delete
	let deleteConfirm  = $state('');
	let deleting       = $state(false);
	let deleteError    = $state('');

	async function load() {
		if (!isOwner) { loading = false; return; }
		try {
			[repoInfo, members] = await Promise.all([
				getRepo(user, repo),
				listMembers(user, repo)
			]);
		} catch (e: any) {
			error = e.message || 'Failed to load settings';
		} finally {
			loading = false;
		}
	}

	async function togglePrivacy() {
		if (!repoInfo) return;
		togglingPrivacy = true;
		try {
			repoInfo = await updateRepo(user, repo, { private: !repoInfo.private });
		} catch (e: any) {
			error = e.message || 'Failed to update visibility';
		} finally {
			togglingPrivacy = false;
		}
	}

	async function toggleZUp() {
		if (!repoInfo) return;
		togglingZUp = true;
		try {
			repoInfo = await updateRepo(user, repo, { z_up: !repoInfo.z_up });
		} catch (e: any) {
			error = e.message || 'Failed to update viewer orientation';
		} finally {
			togglingZUp = false;
		}
	}

	async function toggleResolveLinks() {
		if (!repoInfo) return;
		togglingResolveLinks = true;
		try {
			repoInfo = await updateRepo(user, repo, { resolve_links: !repoInfo.resolve_links });
		} catch (e: any) {
			error = e.message || 'Failed to update assembly setting';
		} finally {
			togglingResolveLinks = false;
		}
	}

	async function handleAddMember() {
		const name = addMemberName.trim();
		if (!name) return;
		addingMember = true;
		addMemberError = '';
		try {
			const m = await addMember(user, repo, name, addMemberCanPush);
			members = [...members.filter((x) => x.username !== m.username), m]
				.sort((a, b) => a.username.localeCompare(b.username));
			addMemberName = '';
			addMemberCanPush = false;
		} catch (e: any) {
			addMemberError = e.message || 'Failed to add collaborator';
		} finally {
			addingMember = false;
		}
	}

	async function handleUpdateMember(member: MemberInfo) {
		try {
			const updated = await addMember(user, repo, member.username, !member.can_push);
			members = members.map((m) => m.username === updated.username ? updated : m);
		} catch (e: any) {
			error = e.message || 'Failed to update collaborator';
		}
	}

	async function handleRemoveMember(username: string) {
		try {
			await removeMember(user, repo, username);
			members = members.filter((m) => m.username !== username);
		} catch (e: any) {
			error = e.message || 'Failed to remove collaborator';
		}
	}

	async function handleDelete() {
		if (deleteConfirm !== repo) return;
		deleting = true;
		deleteError = '';
		try {
			await deleteRepo(user, repo);
			goto(`/${user}`);
		} catch (e: any) {
			deleteError = e.message || 'Failed to delete repository';
			deleting = false;
		}
	}

	onMount(load);
</script>

{#if loading}
	<div class="loading-state">
		<div class="spinner"></div>
		<span>Loading...</span>
	</div>
{:else if !isOwner}
	<div class="alert alert-error">You don't have permission to access settings for this repository.</div>
{:else if error && !repoInfo}
	<div class="alert alert-error">{error}</div>
{:else if repoInfo}
	{#if error}
		<div class="alert alert-error">{error}</div>
	{/if}

	<!-- Visibility -->
	<section class="settings-section">
		<h2 class="section-title">Visibility</h2>
		<div class="settings-card">
			<div class="visibility-row">
				<div class="visibility-info">
					{#if repoInfo.private}
						<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
							<path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zM12 17c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zm3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z"/>
						</svg>
						<div>
							<strong>Private</strong>
							<p>Only you and collaborators can view and clone this repository.</p>
						</div>
					{:else}
						<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
							<circle cx="12" cy="12" r="10"/>
							<line x1="2" y1="12" x2="22" y2="12"/>
							<path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>
						</svg>
						<div>
							<strong>Public</strong>
							<p>Anyone can view and clone this repository. Only collaborators with write access can push.</p>
						</div>
					{/if}
				</div>
				<button class="btn" onclick={togglePrivacy} disabled={togglingPrivacy}>
					{togglingPrivacy ? '...' : repoInfo.private ? 'Make public' : 'Make private'}
				</button>
			</div>
		</div>
	</section>

	<!-- 3D viewer orientation -->
	<section class="settings-section">
		<h2 class="section-title">3D viewer</h2>
		<div class="settings-card">
			<div class="visibility-row">
				<div class="visibility-info">
					<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/>
						<polyline points="3.27 6.96 12 12.01 20.73 6.96"/>
						<line x1="12" y1="22.08" x2="12" y2="12"/>
					</svg>
					<div>
						<strong>Up axis</strong>
						<p>
							Orientation used when rendering STEP/FCStd files in the browser.
							{#if repoInfo.z_up}
								Currently <strong>Z</strong> is treated as up, matching most CAD tools.
							{:else}
								Currently <strong>Y</strong> is treated as up, the default for glTF/Three.js.
							{/if}
						</p>
					</div>
				</div>
				<button class="btn" onclick={toggleZUp} disabled={togglingZUp}>
					{togglingZUp ? '...' : repoInfo.z_up ? 'Use Y up' : 'Use Z up'}
				</button>
			</div>

			<div class="visibility-row viewer-row-divider">
				<div class="visibility-info">
					<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<rect x="3" y="3" width="7" height="7" rx="1"/>
						<rect x="14" y="14" width="7" height="7" rx="1"/>
						<path d="M10 6.5h4a3 3 0 0 1 3 3V14"/>
					</svg>
					<div>
						<strong>Linked assembly</strong>
						<p>
							FreeCAD files can link to shapes defined in other FCStd files (App::Link).
							{#if repoInfo.resolve_links}
								Currently the viewer <strong>resolves links</strong> and shows the full assembly in one model.
							{:else}
								Currently the viewer shows <strong>only the selected file's own shapes</strong> — linked objects are skipped.
							{/if}
						</p>
					</div>
				</div>
				<button class="btn" onclick={toggleResolveLinks} disabled={togglingResolveLinks}>
					{togglingResolveLinks ? '...' : repoInfo.resolve_links ? 'Show file only' : 'Show full assembly'}
				</button>
			</div>
		</div>
	</section>

	<!-- Collaborators -->
	<section class="settings-section">
		<h2 class="section-title">Collaborators</h2>
		<div class="settings-card">
			{#if members.length === 0}
				<p class="empty-msg">No collaborators yet.</p>
			{:else}
				<ul class="member-list">
					{#each members as m (m.username)}
						<li class="member-row">
							<span class="member-name">{m.username}</span>
							<button
								class="role-btn"
								class:role-write={m.can_push}
								onclick={() => handleUpdateMember(m)}
								title="Click to toggle access level"
							>
								{m.can_push ? 'Read & write' : 'Read only'}
							</button>
							<button
								class="btn btn-sm btn-danger"
								onclick={() => handleRemoveMember(m.username)}
							>Remove</button>
						</li>
					{/each}
				</ul>
			{/if}

			{#if addMemberError}
				<div class="alert alert-error">{addMemberError}</div>
			{/if}

			<div class="add-form">
				<input
					type="text"
					class="add-input"
					placeholder="Username"
					bind:value={addMemberName}
					spellcheck="false"
					autocapitalize="off"
					onkeydown={(e) => e.key === 'Enter' && handleAddMember()}
				/>
				<label class="push-label">
					<input type="checkbox" bind:checked={addMemberCanPush} />
					Can push
				</label>
				<button
					class="btn btn-primary"
					onclick={handleAddMember}
					disabled={addingMember || !addMemberName.trim()}
				>
					{addingMember ? 'Adding…' : 'Add collaborator'}
				</button>
			</div>
		</div>
	</section>

	<!-- Danger zone -->
	<section class="settings-section danger-zone">
		<h2 class="section-title danger-title">Danger zone</h2>
		<div class="settings-card danger-card">
			<div class="danger-row">
				<div>
					<strong>Delete this repository</strong>
					<p>This action cannot be undone. All commits, branches, and files will be permanently deleted.</p>
				</div>
			</div>
			<div class="delete-confirm-area">
				<label for="delete-confirm">
					Type <code>{repo}</code> to confirm deletion:
				</label>
				<div class="delete-input-row">
					<input
						id="delete-confirm"
						type="text"
						class="add-input"
						bind:value={deleteConfirm}
						placeholder={repo}
						spellcheck="false"
						autocomplete="off"
					/>
					<button
						class="btn btn-danger-solid"
						onclick={handleDelete}
						disabled={deleting || deleteConfirm !== repo}
					>
						{deleting ? 'Deleting…' : 'Delete repository'}
					</button>
				</div>
				{#if deleteError}
					<div class="alert alert-error" style="margin-top:8px">{deleteError}</div>
				{/if}
			</div>
		</div>
	</section>
{/if}

<style>
	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 24px 0;
		color: var(--color-text-muted);
	}

	.settings-section {
		margin-bottom: 32px;
	}

	.section-title {
		font-size: 16px;
		font-weight: 600;
		margin-bottom: 12px;
		padding-bottom: 8px;
		border-bottom: 1px solid var(--color-border);
	}

	.settings-card {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 20px;
	}

	/* Visibility */
	.visibility-row {
		display: flex;
		align-items: flex-start;
		justify-content: space-between;
		gap: 16px;
	}

	.viewer-row-divider {
		margin-top: 16px;
		padding-top: 16px;
		border-top: 1px solid var(--color-border);
	}

	.visibility-info {
		display: flex;
		align-items: flex-start;
		gap: 12px;
		flex: 1;
	}

	.visibility-info svg {
		flex-shrink: 0;
		margin-top: 2px;
		color: var(--color-text-muted);
	}

	.visibility-info strong {
		display: block;
		font-size: 14px;
		margin-bottom: 4px;
	}

	.visibility-info p {
		font-size: 13px;
		color: var(--color-text-muted);
		margin: 0;
		line-height: 1.5;
	}

	/* Members */
	.empty-msg {
		font-size: 13px;
		color: var(--color-text-muted);
		margin: 0 0 16px;
	}

	.member-list {
		list-style: none;
		padding: 0;
		margin: 0 0 16px;
		display: flex;
		flex-direction: column;
		gap: 6px;
	}

	.member-row {
		display: flex;
		align-items: center;
		gap: 10px;
		padding: 8px 12px;
		background-color: var(--color-bg);
		border: 1px solid var(--color-border);
		border-radius: var(--radius-sm);
	}

	.member-name {
		flex: 1;
		font-size: 14px;
		font-weight: 500;
	}

	.role-btn {
		font-size: 12px;
		padding: 3px 10px;
		border-radius: 20px;
		border: 1px solid var(--color-border);
		background: none;
		color: var(--color-text-muted);
		cursor: pointer;
		transition: border-color 0.1s, color 0.1s;
	}

	.role-btn:hover {
		border-color: var(--color-accent);
		color: var(--color-accent);
	}

	.role-btn.role-write {
		color: var(--color-success, #3fb950);
		border-color: var(--color-success, #3fb950);
	}

	.role-btn.role-write:hover {
		color: var(--color-text-muted);
		border-color: var(--color-border);
	}

	.add-form {
		display: flex;
		align-items: center;
		gap: 10px;
		flex-wrap: wrap;
		padding-top: 16px;
		border-top: 1px solid var(--color-border);
	}

	.add-input {
		flex: 1;
		min-width: 140px;
		padding: 6px 10px;
		font-size: 13px;
		background-color: var(--color-bg);
		border: 1px solid var(--color-border);
		border-radius: var(--radius-sm);
		color: var(--color-text);
		font-family: inherit;
	}

	.add-input:focus {
		outline: none;
		border-color: var(--color-accent);
	}

	.push-label {
		display: flex;
		align-items: center;
		gap: 6px;
		font-size: 13px;
		color: var(--color-text-muted);
		cursor: pointer;
		white-space: nowrap;
	}

	.push-label input {
		accent-color: var(--color-accent);
	}

	/* Danger zone */
	.danger-zone .section-title {
		border-color: rgba(248, 81, 73, 0.4);
	}

	.danger-title {
		color: var(--color-danger, #f85149);
	}

	.danger-card {
		border-color: rgba(248, 81, 73, 0.4);
	}

	.danger-row {
		margin-bottom: 16px;
	}

	.danger-row strong {
		display: block;
		font-size: 14px;
		margin-bottom: 4px;
	}

	.danger-row p {
		font-size: 13px;
		color: var(--color-text-muted);
		margin: 0;
		line-height: 1.5;
	}

	.delete-confirm-area label {
		display: block;
		font-size: 13px;
		color: var(--color-text-muted);
		margin-bottom: 8px;
	}

	.delete-confirm-area code {
		font-family: var(--font-mono);
		font-size: 12px;
		color: var(--color-text);
	}

	.delete-input-row {
		display: flex;
		gap: 10px;
		flex-wrap: wrap;
	}

	.btn-danger {
		color: var(--color-danger, #f85149);
		border-color: var(--color-danger, #f85149);
		background: none;
	}

	.btn-danger:hover {
		background-color: rgba(248, 81, 73, 0.1);
	}

	.btn-danger-solid {
		background-color: var(--color-danger, #f85149);
		color: #fff;
		border: none;
		padding: 6px 14px;
		border-radius: var(--radius-sm);
		font-size: 13px;
		font-family: inherit;
		cursor: pointer;
		white-space: nowrap;
	}

	.btn-danger-solid:disabled {
		opacity: 0.4;
		cursor: not-allowed;
	}

	.btn-danger-solid:not(:disabled):hover {
		filter: brightness(1.1);
	}
</style>
