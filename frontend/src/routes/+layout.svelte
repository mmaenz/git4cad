<script lang="ts">
	import '../app.css';
	import Navbar from '$lib/components/Navbar.svelte';
	import { authStore } from '$lib/stores/auth';
	import { themeStore } from '$lib/stores/theme';
	import { onMount } from 'svelte';

	let { children } = $props();

	onMount(() => {
		authStore.hydrate();
		themeStore.init();
	});
</script>

<svelte:head>
	<title>git4cad</title>
</svelte:head>

<Navbar />

<main>
	{@render children()}
</main>

<style>
	main {
		min-height: calc(100vh - 48px);
	}
</style>
